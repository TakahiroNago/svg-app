FROM golang:1.23-bookworm
RUN go install github.com/bufbuild/buf/cmd/buf@v1.47.2 \
 && go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.1 \
 && go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.5.1
WORKDIR /workspace
