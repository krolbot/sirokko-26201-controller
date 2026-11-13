import test from "node:test";
import assert from "node:assert/strict";
import {
  historyPoints,
  historyPlot,
  combustionText,
} from "../src/lib/history.js";
const data = (points) => ({ boot_id: "1", points });
const point = (t, exhaust = 200000) => [
  t,
  exhaust,
  80000,
  20000,
  2000,
  "RUN",
  0,
];
test("history keeps chronological data and rejects mixed boots, overflow and malformed points", () => {
  assert.equal(historyPoints(data([point(0), point(5000)]), "1").length, 2);
  for (const bad of [
    data([point(5000), point(0)]),
    data([point(0), point(0)]),
    data(Array.from({ length: 361 }, (_, i) => point(i))),
    data([point(-1)]),
    data([point(0, NaN)]),
  ])
    assert.throws(() => historyPoints(bad, "1"));
  assert.throws(() => historyPoints(data([point(0)]), "2"));
});
test("charts break at sensor failures and missing intervals without drawing a false continuous trace", () => {
  const points = historyPoints(
    data([
      point(0),
      point(5000),
      point(10000, null),
      point(15000),
      point(20000),
      point(40000),
    ]),
    "1",
  );
  const plot = historyPlot(points, "exhaust");
  assert.equal(plot.paths.length, 3);
  assert(plot.top > plot.bottom);
  assert(!plot.paths.some((p) => /NaN|Infinity/.test(p)));
});
test("empty, single and disconnected readings have honest chart representations", () => {
  assert.equal(historyPlot([], "exhaust"), null);
  assert.equal(
    historyPlot(historyPoints(data([point(0, null)]), "1"), "exhaust"),
    null,
  );
  const plot = historyPlot(historyPoints(data([point(0)]), "1"), "exhaust");
  assert(plot.lastPoint && Number.isFinite(plot.lastPoint.y));
  const pts = historyPoints(data([point(0), point(5000, null)]), "1");
  assert.equal(historyPlot(pts, "exhaust").lastPoint, null);
});
test("fault markers represent changes and combustion status does not claim measured flame quality", () => {
  const rows = [point(0), point(5000), point(10000), point(15000)];
  rows[1][6] = rows[2][6] = 1024;
  rows[3][6] = 1026;
  assert.equal(
    historyPlot(historyPoints(data(rows), "1"), "exhaust").faults.length,
    2,
  );
  assert.equal(combustionText("WATCHING"), "Контроль температуры выхлопа");
});
