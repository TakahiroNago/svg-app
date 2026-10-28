// Package httpapi exposes the REST API in front of the gRPC conversion engine.
package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"slices"
	"strconv"
	"strings"
	"time"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	svgconvv1 "github.com/TakahiroNago/svg-app/api/gen/svgconv/v1"
	"github.com/TakahiroNago/svg-app/api/internal/imageio"
)

// Engine is the part of the gRPC client the handlers depend on.
type Engine interface {
	Convert(ctx context.Context, req *svgconvv1.ConvertRequest) (*svgconvv1.ConvertResponse, error)
}

// Config holds the tunables of the HTTP layer.
type Config struct {
	AllowedOrigins []string      // exact origins, or "*" to allow any
	EngineTimeout  time.Duration // per-request deadline for the gRPC call
}

var allowedColorCounts = []int{2, 3, 4, 5, 6, 8, 12, 16, 24, 32}

const (
	defaultColors  = 8
	maxKeptColors  = 32
	multipartSlack = 1 << 20 // room for form fields around the file part
)

type server struct {
	engine Engine
	cfg    Config
	log    *slog.Logger
}

// New builds the HTTP handler.
func New(engine Engine, cfg Config, log *slog.Logger) http.Handler {
	if cfg.EngineTimeout <= 0 {
		cfg.EngineTimeout = 60 * time.Second
	}
	if log == nil {
		log = slog.Default()
	}
	s := &server{engine: engine, cfg: cfg, log: log}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", s.health)
	mux.HandleFunc("POST /api/v1/convert", s.convert)
	return s.cors(mux)
}

type paletteColor struct {
	Index int32  `json:"index"`
	Hex   string `json:"hex"`
	Area  uint64 `json:"area"`
}

type convertResponse struct {
	SVG          string         `json:"svg"`
	ContourCount uint64         `json:"contourCount"`
	Palette      []paletteColor `json:"palette"`
}

type errorResponse struct {
	Error string `json:"error"`
}

func (s *server) health(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *server) convert(w http.ResponseWriter, r *http.Request) {
	r.Body = http.MaxBytesReader(w, r.Body, imageio.MaxFileBytes+multipartSlack)
	if err := r.ParseMultipartForm(8 << 20); err != nil {
		var tooBig *http.MaxBytesError
		if errors.As(err, &tooBig) {
			writeError(w, http.StatusRequestEntityTooLarge, "Upload is too large.")
			return
		}
		writeError(w, http.StatusBadRequest, "Request must be multipart/form-data.")
		return
	}
	defer r.MultipartForm.RemoveAll()

	colors, kept, boundary, err := parseOptions(r)
	if err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}

	file, _, err := r.FormFile("file")
	if err != nil {
		writeError(w, http.StatusBadRequest, `Form field "file" is required.`)
		return
	}
	defer file.Close()
	data, err := io.ReadAll(io.LimitReader(file, imageio.MaxFileBytes+1))
	if err != nil {
		writeError(w, http.StatusBadRequest, "Failed to read the uploaded file.")
		return
	}

	img, err := imageio.Decode(data)
	if err != nil {
		switch {
		case errors.Is(err, imageio.ErrTooLarge):
			writeError(w, http.StatusRequestEntityTooLarge, err.Error())
		case errors.Is(err, imageio.ErrUnsupportedType):
			writeError(w, http.StatusUnsupportedMediaType, err.Error())
		default:
			writeError(w, http.StatusUnprocessableEntity, err.Error())
		}
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), s.cfg.EngineTimeout)
	defer cancel()
	resp, err := s.engine.Convert(ctx, &svgconvv1.ConvertRequest{
		Image:      &svgconvv1.Image{Rgba: img.RGBA, Width: int32(img.Width), Height: int32(img.Height)},
		ColorCount: int32(colors),
		KeptColors: kept,
		Boundary:   boundary,
	})
	if err != nil {
		s.writeEngineError(w, r, err)
		return
	}

	out := convertResponse{SVG: resp.GetSvg(), ContourCount: resp.GetContourCount(), Palette: []paletteColor{}}
	for _, c := range resp.GetPalette() {
		out.Palette = append(out.Palette, paletteColor{Index: c.GetIndex(), Hex: c.GetHex(), Area: c.GetArea()})
	}
	writeJSON(w, http.StatusOK, out)
}

func parseOptions(r *http.Request) (colors int, kept []int32, boundary bool, err error) {
	colors = defaultColors
	if v := r.FormValue("colors"); v != "" {
		colors, err = strconv.Atoi(v)
		if err != nil || !slices.Contains(allowedColorCounts, colors) {
			return 0, nil, false, errors.New("colors must be one of 2, 3, 4, 5, 6, 8, 12, 16, 24, 32.")
		}
	}
	if v := r.FormValue("boundary"); v != "" {
		boundary, err = strconv.ParseBool(v)
		if err != nil {
			return 0, nil, false, errors.New("boundary must be true or false.")
		}
	}
	if v := strings.TrimSpace(r.FormValue("kept_colors")); v != "" {
		parts := strings.Split(v, ",")
		if len(parts) > maxKeptColors {
			return 0, nil, false, errors.New("kept_colors has too many entries.")
		}
		for _, p := range parts {
			n, convErr := strconv.Atoi(strings.TrimSpace(p))
			if convErr != nil || n < 0 || n >= colors {
				return 0, nil, false, errors.New("kept_colors must be comma-separated palette indices.")
			}
			kept = append(kept, int32(n))
		}
	}
	return colors, kept, boundary, nil
}

func (s *server) writeEngineError(w http.ResponseWriter, r *http.Request, err error) {
	st, _ := status.FromError(err)
	switch st.Code() {
	case codes.InvalidArgument:
		writeError(w, http.StatusBadRequest, st.Message())
	case codes.ResourceExhausted:
		w.Header().Set("Retry-After", "2")
		writeError(w, http.StatusTooManyRequests, "The conversion engine is busy. Try again shortly.")
	case codes.DeadlineExceeded:
		writeError(w, http.StatusGatewayTimeout, "The conversion timed out.")
	case codes.Canceled:
		// The client went away; nothing useful to send.
		writeError(w, 499, "Request cancelled.")
	case codes.Unavailable:
		s.log.Error("engine unavailable", "err", err, "path", r.URL.Path)
		writeError(w, http.StatusServiceUnavailable, "The conversion engine is unavailable.")
	default:
		s.log.Error("engine error", "code", st.Code(), "err", err, "path", r.URL.Path)
		writeError(w, http.StatusBadGateway, "The conversion failed.")
	}
}

func (s *server) cors(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		if origin != "" && (slices.Contains(s.cfg.AllowedOrigins, "*") || slices.Contains(s.cfg.AllowedOrigins, origin)) {
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Vary", "Origin")
			w.Header().Set("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
			w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
			w.Header().Set("Access-Control-Max-Age", "600")
		}
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, errorResponse{Error: msg})
}
