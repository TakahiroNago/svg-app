// Parity check: the gRPC engine (via the Go API) must produce the same SVG as the WASM engine.
// Needs the WASM build (dist/wasm) and a running stack (docker compose up -d).
//   node --test tests/e2e/parity.test.mjs
// PARITY_API_URL overrides the API address; the test is skipped when the API is unreachable.
import test from "node:test";
import assert from "node:assert/strict";
import { deflateSync } from "node:zlib";
import createSvgConverterModule from "../../dist/wasm/svg-converter.js";

const API = (process.env.PARITY_API_URL ?? "http://localhost:8080").replace(/\/$/, "");

const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}
// 8-bit RGBA PNG (non-premultiplied), so the pixels survive the round trip exactly.
function encodePng(rgba, w, h) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr.set([8, 6, 0, 0, 0], 8);
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    raw.set(rgba.subarray(y * w * 4, (y + 1) * w * 4), y * (w * 4 + 1) + 1);
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", ihdr),
    chunk("IDAT", deflateSync(raw)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

// Three flat regions, a disc, and a semi-transparent stripe.
function fixture() {
  const w = 64, h = 48;
  const data = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let px = x < 24 ? [220, 30, 30, 255] : x < 44 ? [30, 160, 60, 255] : [30, 60, 200, 255];
      if ((x - 32) ** 2 + (y - 24) ** 2 < 100) px = [250, 220, 40, 255];
      if (y > 40) px = [0, 0, 0, 128];
      data.set(px, (y * w + x) * 4);
    }
  }
  return { data, w, h };
}

async function apiUp() {
  try {
    return (await fetch(`${API}/healthz`, { signal: AbortSignal.timeout(2000) })).ok;
  } catch {
    return false;
  }
}

const Module = await createSvgConverterModule();
const up = await apiUp();

for (const boundary of [false, true]) {
  test(`WASM and gRPC engine agree (boundary=${boundary})`, { skip: !up && `API ${API} not reachable` }, async () => {
    const { data, w, h } = fixture();
    const wasm = Module.convertImage(data, w, h, 4, [], boundary);
    assert.equal(wasm.success, true, wasm.error);

    const form = new FormData();
    form.set("file", new Blob([encodePng(data, w, h)], { type: "image/png" }), "fixture.png");
    form.set("colors", "4");
    form.set("boundary", String(boundary));
    const res = await fetch(`${API}/api/v1/convert`, { method: "POST", body: form });
    assert.equal(res.status, 200, await res.clone().text());
    const remote = await res.json();

    assert.equal(remote.svg, wasm.svg);
    assert.equal(remote.contourCount, wasm.contourCount);
  });
}
