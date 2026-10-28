// Package imageio decodes uploaded PNG/JPEG files into the straight RGBA
// layout that the conversion engine expects (same contract as the browser).
package imageio

import (
	"bytes"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/jpeg"
	"image/png"
)

// Limits mirror svg-web/src/converter/limits.ts.
const (
	MaxFileBytes = 20 * 1024 * 1024
	MaxSide      = 4096
	MaxPixels    = 4_000_000
)

var (
	ErrUnsupportedType = errors.New("only PNG and JPEG images are supported")
	ErrTooLarge        = errors.New("image is too large")
	ErrInvalidImage    = errors.New("image could not be decoded")
)

// Decoded holds non-premultiplied RGBA pixels, row-major, tightly packed.
type Decoded struct {
	RGBA   []byte
	Width  int
	Height int
}

// Decode validates the header before decoding pixels so oversized images are
// rejected without allocating their pixel buffers.
func Decode(data []byte) (*Decoded, error) {
	if len(data) > MaxFileBytes {
		return nil, fmt.Errorf("%w: file exceeds %d MB", ErrTooLarge, MaxFileBytes/1024/1024)
	}
	cfg, format, err := image.DecodeConfig(bytes.NewReader(data))
	if err != nil {
		return nil, ErrUnsupportedType
	}
	if format != "png" && format != "jpeg" {
		return nil, ErrUnsupportedType
	}
	if cfg.Width <= 0 || cfg.Height <= 0 {
		return nil, ErrInvalidImage
	}
	if cfg.Width > MaxSide || cfg.Height > MaxSide || cfg.Width*cfg.Height > MaxPixels {
		return nil, fmt.Errorf("%w: %dx%d (max side %d px, max %d pixels)",
			ErrTooLarge, cfg.Width, cfg.Height, MaxSide, MaxPixels)
	}

	var img image.Image
	if format == "png" {
		img, err = png.Decode(bytes.NewReader(data))
	} else {
		img, err = jpeg.Decode(bytes.NewReader(data))
	}
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrInvalidImage, err)
	}
	return toNRGBA(img), nil
}

func toNRGBA(img image.Image) *Decoded {
	b := img.Bounds()
	w, h := b.Dx(), b.Dy()
	out := make([]byte, w*h*4)

	if nrgba, ok := img.(*image.NRGBA); ok {
		for y := 0; y < h; y++ {
			src := nrgba.Pix[y*nrgba.Stride : y*nrgba.Stride+w*4]
			copy(out[y*w*4:], src)
		}
		return &Decoded{RGBA: out, Width: w, Height: h}
	}

	i := 0
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			c := color.NRGBAModel.Convert(img.At(x, y)).(color.NRGBA)
			out[i], out[i+1], out[i+2], out[i+3] = c.R, c.G, c.B, c.A
			i += 4
		}
	}
	return &Decoded{RGBA: out, Width: w, Height: h}
}
