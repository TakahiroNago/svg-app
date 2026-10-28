package main

import (
	"context"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	svgconvv1 "github.com/TakahiroNago/svg-app/api/gen/svgconv/v1"
	"github.com/TakahiroNago/svg-app/api/internal/httpapi"
)

// Largest message the engine accepts (64 MiB of RGBA); keep in sync with the C++ server.
const maxMessageBytes = 80 * 1024 * 1024

type grpcEngine struct {
	client svgconvv1.SvgConverterServiceClient
}

func (g grpcEngine) Convert(ctx context.Context, req *svgconvv1.ConvertRequest) (*svgconvv1.ConvertResponse, error) {
	return g.client.Convert(ctx, req)
}

func env(name, fallback string) string {
	if v := os.Getenv(name); v != "" {
		return v
	}
	return fallback
}

func main() {
	log := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	addr := env("API_ADDR", ":8080")
	engineAddr := env("ENGINE_ADDR", "localhost:50051")

	// grpc.NewClient connects lazily, so the API can start before the engine does.
	conn, err := grpc.NewClient(engineAddr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithDefaultCallOptions(grpc.MaxCallSendMsgSize(maxMessageBytes), grpc.MaxCallRecvMsgSize(maxMessageBytes)),
	)
	if err != nil {
		log.Error("invalid engine address", "addr", engineAddr, "err", err)
		os.Exit(1)
	}
	defer conn.Close()

	var origins []string
	for _, o := range strings.Split(env("ALLOWED_ORIGINS", ""), ",") {
		if o = strings.TrimSpace(o); o != "" {
			origins = append(origins, o)
		}
	}

	srv := &http.Server{
		Addr: addr,
		Handler: httpapi.New(grpcEngine{svgconvv1.NewSvgConverterServiceClient(conn)},
			httpapi.Config{AllowedOrigins: origins}, log),
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       60 * time.Second,
		WriteTimeout:      90 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
		defer cancel()
		_ = srv.Shutdown(shutdownCtx)
	}()

	log.Info("api listening", "addr", addr, "engine", engineAddr, "allowed_origins", origins)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Error("server failed", "err", err)
		os.Exit(1)
	}
}
