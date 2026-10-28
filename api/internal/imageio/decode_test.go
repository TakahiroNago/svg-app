package imageio

import (
	"bytes"
	"errors"
	"image"
	"image/color"
	"image/jpeg"
	"image/png"
	"testing"
)

func encodePNG(t *testing.T, img image.Image) []byte {
	t.Helper()
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func TestDecodePNGKeepsStraightAlpha(t *testing.T) {
	img := image.NewNRGBA(image.Rect(0, 0, 2, 1))
	img.SetNRGBA(0, 0, color.NRGBA{R: 255, G: 0, B: 0, A: 128})
	img.SetNRGBA(1, 0, color.NRGBA{R: 0, G: 0, B: 255, A: 255})

	got, err := Decode(encodePNG(t, img))
	if err != nil {
		t.Fatal(err)
	}
	want := []byte{255, 0, 0, 128, 0, 0, 255, 255}
	if got.Width != 2 || got.Height != 1 || !bytes.Equal(got.RGBA, want) {
		t.Fatalf("got %dx%d %v, want 2x1 %v", got.Width, got.Height, got.RGBA, want)
	}
}

func TestDecodeJPEG(t *testing.T) {
	img := image.NewRGBA(image.Rect(0, 0, 8, 8))
	var buf bytes.Buffer
	if err := jpeg.Encode(&buf, img, nil); err != nil {
		t.Fatal(err)
	}
	got, err := Decode(buf.Bytes())
	if err != nil {
		t.Fatal(err)
	}
	if len(got.RGBA) != 8*8*4 || got.RGBA[3] != 255 {
		t.Fatalf("unexpected output: %d bytes, alpha %d", len(got.RGBA), got.RGBA[3])
	}
}

func TestDecodeRejectsUnsupportedAndInvalid(t *testing.T) {
	if _, err := Decode([]byte("GIF89a not really")); !errors.Is(err, ErrUnsupportedType) {
		t.Fatalf("got %v, want ErrUnsupportedType", err)
	}
	if _, err := Decode(nil); !errors.Is(err, ErrUnsupportedType) {
		t.Fatalf("got %v, want ErrUnsupportedType", err)
	}
	truncated := encodePNG(t, image.NewNRGBA(image.Rect(0, 0, 16, 16)))
	truncated = truncated[:len(truncated)-20]
	if _, err := Decode(truncated); !errors.Is(err, ErrInvalidImage) {
		t.Fatalf("got %v, want ErrInvalidImage", err)
	}
}

func TestDecodeRejectsOversizedDimensions(t *testing.T) {
	// A blank image compresses to a tiny file, so only the dimension check can reject it.
	data := encodePNG(t, image.NewGray(image.Rect(0, 0, MaxSide+1, 1)))
	if _, err := Decode(data); !errors.Is(err, ErrTooLarge) {
		t.Fatalf("side: got %v, want ErrTooLarge", err)
	}
	data = encodePNG(t, image.NewGray(image.Rect(0, 0, 2001, 2000)))
	if _, err := Decode(data); !errors.Is(err, ErrTooLarge) {
		t.Fatalf("pixels: got %v, want ErrTooLarge", err)
	}
}
