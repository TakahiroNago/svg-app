//go:build integration

// Runs against a live stack: docker compose up -d, then
//
//	API_URL=http://localhost:8080 go test -tags integration ./test/integration
package integration

import (
	"bytes"
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"strconv"
	"strings"
	"testing"
	"time"
)

func apiURL() string {
	if v := os.Getenv("API_URL"); v != "" {
		return strings.TrimRight(v, "/")
	}
	return "http://localhost:8080"
}

// Left half red, right half blue; same fixture as svg-app/tests/wasm.test.mjs.
func twoColorPNG(t *testing.T, w, h int) []byte {
	t.Helper()
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			if x < w/2 {
				img.SetNRGBA(x, y, color.NRGBA{R: 255, A: 255})
			} else {
				img.SetNRGBA(x, y, color.NRGBA{B: 255, A: 255})
			}
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func post(t *testing.T, file []byte, fields map[string]string) (*http.Response, []byte) {
	t.Helper()
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	part, _ := mw.CreateFormFile("file", "in.png")
	_, _ = part.Write(file)
	for k, v := range fields {
		_ = mw.WriteField(k, v)
	}
	_ = mw.Close()

	req, _ := http.NewRequest(http.MethodPost, apiURL()+"/api/v1/convert", &body)
	req.Header.Set("Content-Type", mw.FormDataContentType())
	resp, err := (&http.Client{Timeout: 60 * time.Second}).Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	return resp, data
}

func TestHealth(t *testing.T) {
	resp, err := http.Get(apiURL() + "/healthz")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status %d", resp.StatusCode)
	}
}

func TestConvertThroughEngine(t *testing.T) {
	resp, data := post(t, twoColorPNG(t, 32, 16), map[string]string{"colors": "2"})
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status %d: %s", resp.StatusCode, data)
	}
	var got struct {
		SVG          string `json:"svg"`
		ContourCount int    `json:"contourCount"`
		Palette      []struct {
			Hex  string `json:"hex"`
			Area int    `json:"area"`
		} `json:"palette"`
	}
	if err := json.Unmarshal(data, &got); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(got.SVG, "<svg") {
		t.Fatalf("no svg in response: %s", data)
	}
	// RGBA red must stay red (not swapped to blue).
	for _, want := range []string{"#ff0000", "#0000ff"} {
		if !strings.Contains(strings.ToLower(got.SVG), want) {
			t.Errorf("svg is missing %s", want)
		}
	}
	if len(got.Palette) != 2 || got.ContourCount == 0 {
		t.Errorf("unexpected palette/contours: %+v", got)
	}
}

func TestConvertBoundaryAndKeptColors(t *testing.T) {
	file := twoColorPNG(t, 32, 16)
	resp, data := post(t, file, map[string]string{"colors": "2", "boundary": "true"})
	if resp.StatusCode != http.StatusOK || !strings.Contains(string(data), `fill=\"none\"`) {
		t.Fatalf("boundary mode: %d %s", resp.StatusCode, data)
	}

	// Keeping only the largest palette entry must drop the other region.
	resp, data = post(t, file, map[string]string{"colors": "2"})
	var full struct {
		Palette []struct {
			Index int `json:"index"`
		} `json:"palette"`
	}
	if err := json.Unmarshal(data, &full); err != nil || len(full.Palette) != 2 {
		t.Fatalf("palette: %v %s", err, data)
	}
	resp, data = post(t, file, map[string]string{
		"colors": "2", "kept_colors": strconv.Itoa(full.Palette[0].Index),
	})
	var kept struct {
		SVG string `json:"svg"`
	}
	if err := json.Unmarshal(data, &kept); err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK || strings.Count(kept.SVG, "<path") != 1 {
		t.Fatalf("kept colors: %d %s", resp.StatusCode, data)
	}
}

func TestInvalidInputIsRejectedByAPI(t *testing.T) {
	resp, _ := post(t, []byte("not an image"), nil)
	if resp.StatusCode != http.StatusUnsupportedMediaType {
		t.Fatalf("status %d", resp.StatusCode)
	}
}
