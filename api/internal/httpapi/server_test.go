package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"io"
	"log/slog"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"testing"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	svgconvv1 "github.com/TakahiroNago/svg-app/api/gen/svgconv/v1"
)

type fakeEngine struct {
	req  *svgconvv1.ConvertRequest
	resp *svgconvv1.ConvertResponse
	err  error
}

func (f *fakeEngine) Convert(_ context.Context, req *svgconvv1.ConvertRequest) (*svgconvv1.ConvertResponse, error) {
	f.req = req
	return f.resp, f.err
}

func pngBytes(t *testing.T) []byte {
	t.Helper()
	img := image.NewNRGBA(image.Rect(0, 0, 4, 2))
	for x := 0; x < 4; x++ {
		img.SetNRGBA(x, 0, color.NRGBA{R: 255, A: 255})
		img.SetNRGBA(x, 1, color.NRGBA{B: 255, A: 255})
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func upload(t *testing.T, h http.Handler, file []byte, fields map[string]string) *httptest.ResponseRecorder {
	t.Helper()
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	if file != nil {
		part, _ := mw.CreateFormFile("file", "in.png")
		_, _ = part.Write(file)
	}
	for k, v := range fields {
		_ = mw.WriteField(k, v)
	}
	_ = mw.Close()
	req := httptest.NewRequest(http.MethodPost, "/api/v1/convert", &body)
	req.Header.Set("Content-Type", mw.FormDataContentType())
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)
	return rec
}

func newHandler(e Engine) http.Handler {
	return New(e, Config{AllowedOrigins: []string{"http://localhost:5173"}}, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func TestConvertSuccess(t *testing.T) {
	eng := &fakeEngine{resp: &svgconvv1.ConvertResponse{
		Svg:          "<svg/>",
		ContourCount: 2,
		Palette:      []*svgconvv1.PaletteColor{{Index: 1, Hex: "#ff0000", Area: 4}},
	}}
	rec := upload(t, newHandler(eng), pngBytes(t), map[string]string{
		"colors": "2", "boundary": "true", "kept_colors": "0,1",
	})
	if rec.Code != http.StatusOK {
		t.Fatalf("status %d: %s", rec.Code, rec.Body.String())
	}
	var got convertResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got.SVG != "<svg/>" || got.ContourCount != 2 || len(got.Palette) != 1 || got.Palette[0].Hex != "#ff0000" {
		t.Fatalf("unexpected body: %+v", got)
	}
	r := eng.req
	if r.GetColorCount() != 2 || !r.GetBoundary() || len(r.GetKeptColors()) != 2 {
		t.Fatalf("options not forwarded: %+v", r)
	}
	img := r.GetImage()
	if img.GetWidth() != 4 || img.GetHeight() != 2 || len(img.GetRgba()) != 4*2*4 {
		t.Fatalf("image not forwarded: %dx%d, %d bytes", img.GetWidth(), img.GetHeight(), len(img.GetRgba()))
	}
	if !bytes.Equal(img.GetRgba()[:4], []byte{255, 0, 0, 255}) {
		t.Fatalf("pixels are not straight RGBA: %v", img.GetRgba()[:4])
	}
}

func TestConvertDefaultsToEightColors(t *testing.T) {
	eng := &fakeEngine{resp: &svgconvv1.ConvertResponse{}}
	rec := upload(t, newHandler(eng), pngBytes(t), nil)
	if rec.Code != http.StatusOK || eng.req.GetColorCount() != 8 {
		t.Fatalf("status %d, colors %d", rec.Code, eng.req.GetColorCount())
	}
}

func TestConvertRejectsBadInput(t *testing.T) {
	tests := []struct {
		name   string
		file   []byte
		fields map[string]string
		want   int
	}{
		{"missing file", nil, nil, http.StatusBadRequest},
		{"unsupported type", []byte("not an image"), nil, http.StatusUnsupportedMediaType},
		{"bad colors", pngBytes(t), map[string]string{"colors": "7"}, http.StatusBadRequest},
		{"non numeric colors", pngBytes(t), map[string]string{"colors": "x"}, http.StatusBadRequest},
		{"bad boundary", pngBytes(t), map[string]string{"boundary": "maybe"}, http.StatusBadRequest},
		{"kept out of range", pngBytes(t), map[string]string{"colors": "2", "kept_colors": "5"}, http.StatusBadRequest},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			eng := &fakeEngine{resp: &svgconvv1.ConvertResponse{}}
			rec := upload(t, newHandler(eng), tc.file, tc.fields)
			if rec.Code != tc.want {
				t.Fatalf("status %d, want %d: %s", rec.Code, tc.want, rec.Body.String())
			}
			if eng.req != nil {
				t.Fatal("engine must not be called for invalid input")
			}
		})
	}
}

func TestEngineErrorMapping(t *testing.T) {
	tests := []struct {
		code codes.Code
		want int
	}{
		{codes.InvalidArgument, http.StatusBadRequest},
		{codes.ResourceExhausted, http.StatusTooManyRequests},
		{codes.DeadlineExceeded, http.StatusGatewayTimeout},
		{codes.Unavailable, http.StatusServiceUnavailable},
		{codes.Internal, http.StatusBadGateway},
	}
	for _, tc := range tests {
		t.Run(tc.code.String(), func(t *testing.T) {
			eng := &fakeEngine{err: status.Error(tc.code, "boom")}
			rec := upload(t, newHandler(eng), pngBytes(t), nil)
			if rec.Code != tc.want {
				t.Fatalf("status %d, want %d", rec.Code, tc.want)
			}
		})
	}
}

func TestHealthAndCORS(t *testing.T) {
	h := newHandler(&fakeEngine{})

	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, "/healthz", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("healthz status %d", rec.Code)
	}

	pre := httptest.NewRequest(http.MethodOptions, "/api/v1/convert", nil)
	pre.Header.Set("Origin", "http://localhost:5173")
	rec = httptest.NewRecorder()
	h.ServeHTTP(rec, pre)
	if rec.Code != http.StatusNoContent || rec.Header().Get("Access-Control-Allow-Origin") != "http://localhost:5173" {
		t.Fatalf("preflight: %d %v", rec.Code, rec.Header())
	}

	evil := httptest.NewRequest(http.MethodGet, "/healthz", nil)
	evil.Header.Set("Origin", "https://evil.example")
	rec = httptest.NewRecorder()
	h.ServeHTTP(rec, evil)
	if rec.Header().Get("Access-Control-Allow-Origin") != "" {
		t.Fatal("unlisted origin must not be allowed")
	}
}
