// Run after building the WASM module: node --test tests/wasm.test.mjs
import test from "node:test";
import assert from "node:assert/strict";
import createSvgConverterModule from "../dist/wasm/svg-converter.js";

const Module = await createSvgConverterModule();

// Left half red, right half blue, 8x4.
function twoColorImage() {
  const w = 8, h = 4;
  const data = new Uint8ClampedArray(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const left = i % w < w / 2;
    data.set(left ? [255, 0, 0, 255] : [0, 0, 255, 255], i * 4);
  }
  return { data, w, h };
}

test("converts RGBA to SVG with correct channel order", () => {
  const { data, w, h } = twoColorImage();
  const r = Module.convertImage(data, w, h, 2, [], false);
  assert.equal(r.success, true, r.error);
  assert.match(r.svg, /<svg/);
  assert.match(r.svg, /#ff0000/i); // RGBA red must stay red, not become blue
  assert.match(r.svg, /#0000ff/i);
});

test("boundary mode works", () => {
  const { data, w, h } = twoColorImage();
  const r = Module.convertImage(data, w, h, 2, [], true);
  assert.equal(r.success, true, r.error);
  assert.match(r.svg, /<svg/);
});

test("semi-transparent alpha blends with white", () => {
  // red at alpha 128 over white: G/B = (255*127 + 127)/255 = 127 -> #ff7f7f
  const w = 8, h = 4;
  const data = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const left = i % w < w / 2;
    data.set(left ? [255, 0, 0, 128] : [0, 0, 255, 255], i * 4);
  }
  const session = new Module.SvgSession();
  try {
    const a = session.analyze(data, w, h, 2);
    assert.equal(a.success, true, a.error);
    const hexes = a.palette.map((c) => c.hex.toLowerCase());
    assert.ok(hexes.includes("#ff7f7f"), hexes.join(","));
    assert.ok(hexes.includes("#0000ff"), hexes.join(","));
  } finally {
    session.delete();
  }
});

test("fully transparent pixels become white", () => {
  const w = 8, h = 4;
  const data = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const left = i % w < w / 2;
    data.set(left ? [255, 0, 0, 0] : [0, 0, 255, 255], i * 4); // transparent red
  }
  const session = new Module.SvgSession();
  try {
    const a = session.analyze(data, w, h, 2);
    assert.equal(a.success, true, a.error);
    const hexes = a.palette.map((c) => c.hex.toLowerCase());
    assert.ok(hexes.includes("#ffffff"), hexes.join(","));
    assert.ok(!hexes.includes("#ff0000"), hexes.join(","));
  } finally {
    session.delete();
  }
});

test("session: select kept colors and regenerate", () => {
  const { data, w, h } = twoColorImage();
  const session = new Module.SvgSession();
  try {
    const a = session.analyze(data, w, h, 2);
    assert.equal(a.success, true, a.error);
    assert.equal(a.palette.length, 2);
    const g = session.generate([a.palette[0].index], false);
    assert.equal(g.success, true, g.error);
    assert.ok(g.svg.length > 0);
    const bad = session.generate([99], false);
    assert.equal(bad.success, false);
    assert.ok(bad.error.length > 0);
    assert.equal(session.generate([1.5], false).success, false);
  } finally {
    session.delete();
  }
});

test("generate before analyze fails", () => {
  const session = new Module.SvgSession();
  try {
    const g = session.generate([], false);
    assert.equal(g.success, false);
    assert.ok(g.error);
  } finally {
    session.delete();
  }
});

test("rejects invalid input", () => {
  const { data, w, h } = twoColorImage();
  const cases = [
    ["zero width", () => Module.convertImage(data, 0, h, 2, [], false)],
    ["negative height", () => Module.convertImage(data, w, -1, 2, [], false)],
    ["empty data", () => Module.convertImage(new Uint8Array(0), w, h, 2, [], false)],
    ["size mismatch", () => Module.convertImage(data.subarray(0, 16), w, h, 2, [], false)],
    ["not typed array", () => Module.convertImage([1, 2, 3], w, h, 2, [], false)],
    ["null data", () => Module.convertImage(null, w, h, 2, [], false)],
    ["bad color count 7", () => Module.convertImage(data, w, h, 7, [], false)],
    ["color count 0", () => Module.convertImage(data, w, h, 0, [], false)],
    ["too many colors for image", () => Module.convertImage(new Uint8Array(2 * 2 * 4), 2, 2, 32, [], false)],
    ["huge dimensions", () => Module.convertImage(data, 100000, 100000, 2, [], false)],
    ["kept not array", () => Module.convertImage(data, w, h, 2, "x", false)],
  ];
  for (const [name, run] of cases) {
    const r = run();
    assert.equal(r.success, false, name);
    assert.ok(r.error && r.error.length > 0, `${name}: error message`);
  }
});

test("respects Uint8Array view offsets", () => {
  const { data, w, h } = twoColorImage();
  const padded = new Uint8Array(data.length + 10);
  padded.set(data, 10);
  const r = Module.convertImage(padded.subarray(10), w, h, 2, [], false);
  assert.equal(r.success, true, r.error);
  assert.match(r.svg, /#ff0000/i);
});
