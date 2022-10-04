package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"sync/atomic"
	"syscall"
	"time"
)

const TIMEOUT_DURATION = 30 * time.Second

type Runner struct {
	net.Dialer

	connected atomic.Bool
}

type ConnectResult struct {
	Conn  net.Conn
	Error error
}

var ErrConnectFailed = errors.New("failed to connect to host(s)")

func main() {
	if len(os.Args) < 3 {
		print("Usage: %s <port> <host> [alt-hosts]\n", os.Args[0])
		os.Exit(1)
	}

	ctx, cancel := context.WithCancel(context.Background())
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigCh
		cancel()
	}()

	runner := Runner{
		Dialer: net.Dialer{
			Timeout: TIMEOUT_DURATION,
		},
	}
	port, hosts := os.Args[1], os.Args[2:]
	if err := runner.Run(ctx, port, hosts...); err != nil {
		if errors.Is(ErrConnectFailed, err) {
			print("Host(s) unavailable\n")
		} else {
			panic(err)
		}
		os.Exit(1)
	}
}

func (r *Runner) Run(ctx context.Context, port string, hosts ...string) error {
	conn, err := r.connect(ctx, port, hosts...)
	if err != nil {
		return err
	}
	<-ctx.Done()
	return conn.Close()
}

func (r *Runner) connect(ctx context.Context, port string, hosts ...string) (net.Conn, error) {
	ch := make(chan ConnectResult, 1)
	counter := 0
	for _, host := range hosts {
		go r.connectRoutine(ctx, ch, host, port)
		counter++
	}
	var conn net.Conn
	for ; counter > 0; counter-- {
		res := <-ch
		if res.Conn != nil {
			conn = res.Conn
		}
	}
	close(ch)
	if conn != nil {
		return conn, nil
	}
	return nil, ErrConnectFailed
}

func (r *Runner) connectRoutine(ctx context.Context, ch chan<- ConnectResult, host, port string) {
	conn, err := r.DialContext(ctx, "tcp", net.JoinHostPort(host, port))
	if err != nil {
		ch <- ConnectResult{nil, err}
	} else if r.connected.CompareAndSwap(false, true) {
		print("Connected to %s\n", host)
		go pipe(conn, os.Stdin)
		go pipe(os.Stdout, conn)
		ch <- ConnectResult{conn, nil}
	} else {
		conn.Close()
		ch <- ConnectResult{}
	}
}

func pipe(dst io.Writer, src io.Reader) {
	buf := make([]byte, 512)
	io.CopyBuffer(dst, src, buf)
}

func print(format string, a ...any) {
	fmt.Fprintf(os.Stderr, format, a...)
}
