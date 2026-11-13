export function historyPoints(data, bootId) {
  if (
    data?.boot_id !== bootId ||
    !Array.isArray(data.points) ||
    data.points.length > 360
  )
    throw new Error("История относится к другой загрузке или повреждена");
  let previous = -1;
  return data.points.map((row) => {
    if (
      !Array.isArray(row) ||
      row.length !== 7 ||
      !Number.isSafeInteger(row[0]) ||
      row[0] <= previous ||
      row
        .slice(1, 4)
        .some(
          (v) =>
            v !== null &&
            (!Number.isInteger(v) || v < -2147483648 || v > 2147483647),
        ) ||
      !Number.isInteger(row[4]) ||
      row[4] < 0 ||
      row[4] > 4014 ||
      typeof row[5] !== "string" ||
      !Number.isSafeInteger(row[6]) ||
      row[6] < 0
    )
      throw new Error("Некорректная история измерений");
    previous = row[0];
    return {
      at: row[0],
      exhaust: row[1],
      air: row[2],
      room: row[3],
      speed: row[4],
      state: row[5],
      faults: row[6],
    };
  });
}
export function historyPlot(points, channel, width = 608) {
  const values = points.map((p) => p[channel]).filter((v) => v !== null);
  if (!values.length) return null;
  const first = points[0].at,
    last = points.at(-1).at;
  const low = Math.min(...values) / 1000,
    high = Math.max(...values) / 1000;
  const padding = Math.max(2, (high - low) * 0.12);
  const bottom = Math.floor(low - padding),
    top = Math.ceil(high + padding);
  const x = (at) =>
    40 +
    ((Math.max(200, width) - 56) * (at - first)) / Math.max(1, last - first);
  const y = (value) => 174 - (158 * (value / 1000 - bottom)) / (top - bottom);
  const paths = [];
  let path = "",
    previous = null;
  for (const point of points) {
    if (
      point[channel] === null ||
      (previous !== null && point.at - previous > 10000)
    ) {
      if (path) paths.push(path);
      path = "";
    }
    if (point[channel] !== null)
      path += `${path ? " L" : "M"}${x(point.at).toFixed(2)},${y(point[channel]).toFixed(2)}`;
    previous = point.at;
  }
  if (path) paths.push(path);
  return {
    paths,
    top,
    bottom,
    first,
    last,
    lastPoint:
      points.at(-1)[channel] === null
        ? null
        : { x: x(last), y: y(points.at(-1)[channel]) },
    faults: points
      .filter((p, i) => p.faults && (!i || p.faults !== points[i - 1].faults))
      .map((p) => x(p.at)),
  };
}
export function combustionText(phase) {
  return (
    {
      LEARNING: "Сбор температур для контроля падения выхлопа",
      WATCHING: "Контроль температуры выхлопа",
      SUSPECT: "Проверка падения температуры выхлопа",
      LOST: "Выхлоп устойчиво остывает при неизменном ШИМ",
    }[phase] || ""
  );
}
