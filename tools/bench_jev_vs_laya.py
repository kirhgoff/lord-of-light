"""Benchmark TypeSafe's hosted Jev against Laya (local and LAN) with the request bodies the game sends.

Usage (run from the repo root):
    build/headless --seed 1 --ticks 300 --dump-requests build/requests-seed1.jsonl
    uv run --python 3.12 python tools/bench_jev_vs_laya.py --corpus build/requests-seed1.jsonl --size 60
    uv run --python 3.12 python tools/bench_jev_vs_laya.py --backends laya-local --size 3
    uv run --python 3.12 python tools/bench_jev_vs_laya.py --self-test

Needs curl on PATH (stdlib sockets lack macOS local-network permission; curl also keeps the key off the command line).
Writes one JSONL row per request to build/ and a self-contained HTML report to
~/Downloads/_reports/. The Jev API key is read from .jev_key (gitignored, found through the
main checkout) and is asserted absent from every output before anything is written.
"""
import argparse
import html
import json
import math
import os
import statistics
import subprocess
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import date

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JEV_URL = "https://api.typesafe.ai"
BACKOFF_SECONDS = (0.5, 1, 2)
RETRY_STATUSES = (429, 529)
QUESTION_IDS = ("hurt", "element", "stance")
BACKEND_COLORS = {"jev": "var(--c1)", "laya-tomhat": "var(--c2)", "laya-local": "var(--c3)"}
BACKEND_ORDER = ("jev", "laya-tomhat", "laya-local")
ERROR_CLASSES = ("connect", "timeout", "http3xx", "http4xx", "http5xx", "malformed", "unknown choice")


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(values)
    rank = (len(ordered) - 1) * p / 100
    low, high = math.floor(rank), math.ceil(rank)
    return ordered[low] + (ordered[high] - ordered[low]) * (rank - low)


def margin(probabilities):
    k = len(probabilities)
    if k < 2:
        return 1.0
    return (k * max(probabilities.values()) - 1) / (k - 1)


def agreement(rows_a, rows_b, question):
    pairs = [(a["answers"][question]["choice"], b["answers"][question]["choice"])
             for a, b in zip(rows_a, rows_b)
             if question in a["answers"] and question in b["answers"]]
    return (sum(x == y for x, y in pairs), len(pairs))


def reflex_agreement(rows, question):
    pairs = [(r["answers"][question]["choice"], r["reflex"].get(question))
             for r in rows if question in r["answers"] and r["reflex"].get(question) is not None]
    return (sum(x == y for x, y in pairs), len(pairs))


def stance_margins(rows):
    return [r["answers"]["stance"]["margin_recomputed"] for r in rows if "stance" in r["answers"]]


def clear_rate(margins, threshold):
    return sum(m >= threshold for m in margins) / len(margins) if margins else None


def histogram(margins, bins=10):
    counts = [0] * bins
    for m in margins:
        counts[min(bins - 1, max(0, int(m * bins)))] += 1
    return counts


def game_would_use_rate(rows, threshold):
    if not rows:
        return None
    used = sum(r["understood"] and r["answers"]["stance"]["margin_recomputed"] >= threshold for r in rows)
    return used / len(rows)


def timing_summary(rows, field="wall_ms"):
    walls = [r[field] for r in rows if r["phase"] == "warm" and r["error"] is None]
    return {
        "p50": percentile(walls, 50), "p95": percentile(walls, 95),
        "max": max(walls) if walls else None, "mean": statistics.fmean(walls) if walls else None,
    }


CURL_TIMEOUT_EXIT = 28
CURL_FAILURES = {6: "could not resolve host", 7: "connection failed", 28: "timed out", 52: "empty reply", 56: "connection reset"}


def classify_error(status, curl_exit):
    if curl_exit:
        return "timeout" if curl_exit == CURL_TIMEOUT_EXIT else "connect"
    if status is not None and 300 <= status < 400:
        return "http3xx"
    if status is not None and 400 <= status < 500:
        return "http4xx"
    if status is not None and status >= 500:
        return "http5xx"
    return None


def quoted(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def http_call(method, url, headers, body, timeout_s):
    config = ["silent", "include", "url = " + quoted(url), "request = " + method, "max-time = %s" % timeout_s,
              "write-out = " + quoted("\n%{http_code} %{time_total} %{time_connect} %{time_appconnect}")]
    config += ["header = " + quoted("%s: %s" % item) for item in headers.items()]
    if body is not None:
        config.append("data-binary = " + quoted(json.dumps(body, separators=(",", ":"), ensure_ascii=False)))
    started = time.perf_counter()
    done = subprocess.run(["curl", "--config", "-"], input="\n".join(config).encode(), capture_output=True)
    elapsed_ms = (time.perf_counter() - started) * 1000
    reply, _, trailer = done.stdout.decode("utf-8", "replace").rpartition("\n")
    head, _, payload = reply.partition("\r\n\r\n")
    fields = trailer.split()
    status = int(fields[0]) if fields and fields[0].isdigit() and int(fields[0]) else None
    times = [float(f) * 1000 for f in fields[1:4]] if len(fields) == 4 else []
    wall_ms = times[0] if times and times[0] > 0 else elapsed_ms
    handshake_ms = max(times[1:], default=0.0)
    response_headers = {}
    for line in head.split("\r\n")[1:]:
        name, _, value = line.partition(":")
        response_headers[name.strip().lower()] = value.strip()
    return {"status": status, "payload": payload, "headers": response_headers, "curl_exit": done.returncode,
            "wall_ms": wall_ms, "handshake_ms": min(handshake_ms, wall_ms)}


def read_key(path):
    with open(path) as f:
        return f.read().strip()


def default_key_path():
    try:
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True, check=True).stdout.strip()
        return os.path.join(os.path.dirname(common), ".jev_key")
    except (subprocess.CalledProcessError, OSError):
        return os.path.join(ROOT, ".jev_key")


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                              check=True).stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        return "unknown"


def load_corpus(paths, size):
    records = []
    for path in paths:
        with open(path) as f:
            records += [json.loads(line) for line in f if line.strip()]
    if size and size < len(records):
        records = [records[i * len(records) // size] for i in range(size)]
    return records


def backend_definitions(config, selected):
    names = ["jev"]
    urls = {"jev": JEV_URL}
    for url in config["urls"]:
        name = "laya-local" if "127.0.0.1" in url or "localhost" in url else "laya-tomhat"
        names.append(name)
        urls[name] = url
    chosen = [n for n in names if selected is None or n in selected]
    return [(n, urls[n]) for n in chosen]


def probe(name, url, key, timeout_s):
    if name == "jev":
        result = http_call("GET", url + "/v1/models", {"Authorization": "Bearer " + key}, None, timeout_s)
        if result["curl_exit"] or result["status"] in (401, 403):
            return None, describe_failure(result)
        return {}, None
    result = http_call("GET", url + "/health", {}, None, timeout_s)
    if result["curl_exit"] or result["status"] != 200:
        return None, describe_failure(result)
    try:
        health = json.loads(result["payload"])
    except ValueError:
        return None, "health is not JSON"
    return {"revision": (health.get("revisions") or {}).get("english", "")[:8], "device": health.get("device")}, None


def describe_failure(result):
    if result["curl_exit"]:
        return CURL_FAILURES.get(result["curl_exit"], "curl exit %d" % result["curl_exit"])
    return "HTTP %s" % result["status"]


def parse_answers(reply, request):
    answers = {}
    for qid, answer in (reply.get("answers") or {}).items():
        if not isinstance(answer, dict) or "choice" not in answer:
            continue
        probabilities = answer.get("probabilities") or {}
        criteria = request["questions"].get(qid, {}).get("criteria", {})
        reported = answer.get("x_jev_confidence", answer.get("confidence"))
        answers[qid] = {
            "choice": answer["choice"], "probabilities": probabilities,
            "margin_reported": reported,
            "margin_recomputed": margin(probabilities) if probabilities else 0.0,
            "laya_confidence": answer.get("confidence") if "x_jev_confidence" in answer else None,
            "laya_answer_confidence": answer.get("answer_confidence"),
            "known_choice": answer["choice"] in criteria,
        }
    return answers


def understood(answers, request):
    asked = [q for q in QUESTION_IDS if q in request["questions"]]
    return bool(asked) and all(q in answers and answers[q]["known_choice"] for q in asked)


def send_one(name, url, key, model, timeout_s, index, record, phase):
    request = record["request"]
    body = dict(request, model=model) if name == "jev" else request
    headers = {"Content-Type": "application/json"}
    if name == "jev":
        headers["Authorization"] = "Bearer " + key
    retries = 0
    while True:
        result = http_call("POST", url + "/v1/systemone", headers, body, timeout_s)
        if result["status"] in RETRY_STATUSES and retries < len(BACKOFF_SECONDS):
            time.sleep(BACKOFF_SECONDS[retries])
            retries += 1
            continue
        break
    row = {"backend": name, "index": index, "tick": record["tick"], "combatant": record["combatant"],
           "name": record["name"], "phase": phase, "wall_ms": result["wall_ms"], "handshake_ms": result["handshake_ms"],
           "reused_ms": result["wall_ms"] - result["handshake_ms"], "server_ms": None,
           "http_status": result["status"], "retries": retries, "error": None, "error_detail": None,
           "model": None, "input_tokens": None, "output_tokens": None, "truncated": None,
           "answers": {}, "answer_fields": [], "usage_fields": [], "understood": False, "reflex": record["reflex"], "state_chars": len(request["state"]),
           "request": request}
    server = result["headers"].get("x-inference-time-ms")
    row["server_ms"] = float(server) if server else None
    failure = classify_error(result["status"], result["curl_exit"])
    if failure is None and result["status"] != 200:
        failure = "malformed"
    if failure:
        row["error"] = failure
        row["error_detail"] = describe_failure(result) + " " + result["payload"][:200]
        return row
    try:
        reply = json.loads(result["payload"])
    except ValueError:
        row["error"], row["error_detail"] = "malformed", result["payload"][:200]
        return row
    usage = reply.get("usage") or {}
    row.update(model=reply.get("model"), input_tokens=usage.get("input_tokens"), output_tokens=usage.get("output_tokens"),
               truncated=usage.get("truncated"), answers=parse_answers(reply, request),
               answer_fields=sorted({k for a in (reply.get("answers") or {}).values() if isinstance(a, dict) for k in a}),
               usage_fields=sorted(usage))
    row["understood"] = understood(row["answers"], request)
    if not row["understood"]:
        row["error"], row["error_detail"] = "unknown choice", result["payload"][:200]
    return row


def run_backend(name, url, key, model, timeout_s, records, concurrency):
    jobs = [(i, r, "cold" if i == 0 else "warm") for i, r in enumerate(records)]
    if name == "jev" and concurrency > 1:
        with ThreadPoolExecutor(concurrency) as pool:
            return list(pool.map(lambda j: send_one(name, url, key, model, timeout_s, *j), jobs))
    return [send_one(name, url, key, model, timeout_s, *j) for j in jobs]


def fmt(value, digits=0, suffix=""):
    return "-" if value is None else ("%.*f" % (digits, value)) + suffix


def pct(value):
    return "-" if value is None else "%.0f%%" % (value * 100)


def table(headers, rows):
    head = "".join("<th>%s</th>" % html.escape(str(h)) for h in headers)
    body = "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % html.escape(str(c)) for c in row) for row in rows)
    return "<div class=scroll><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>" % (head, body)


def legend(names):
    return "<div class=legend>%s</div>" % "".join(
        "<span><i style='background:%s'></i>%s</span>" % (BACKEND_COLORS[n], n) for n in names)


def svg_grouped_bars(stats, names):
    metrics = ("p50", "p95", "max")
    top = max([stats[n][m] or 0 for n in names for m in metrics] + [1])
    width, height, left, bottom = 640, 280, 50, 30
    plot_h = height - bottom - 20
    group_w = (width - left) / len(metrics)
    bar_w = group_w / (len(names) + 1)
    parts = ["<svg viewBox='0 0 %d %d' role=img aria-label='Latency p50, p95 and max per backend'>" % (width, height)]
    for tick in range(5):
        value = top * tick / 4
        y = 20 + plot_h - plot_h * tick / 4
        parts.append("<line class=grid x1=%d x2=%d y1=%.1f y2=%.1f /><text class=axis x=%d y=%.1f text-anchor=end>%.0f</text>"
                     % (left, width, y, y, left - 6, y + 4, value))
    for gi, metric in enumerate(metrics):
        gx = left + gi * group_w
        parts.append("<text class=axis x=%.1f y=%d text-anchor=middle>%s ms</text>" % (gx + group_w / 2, height - 8, metric))
        for bi, n in enumerate(names):
            value = stats[n][metric] or 0
            h = plot_h * value / top
            x = gx + bar_w * (bi + 0.5)
            parts.append("<rect x=%.1f y=%.1f width=%.1f height=%.1f rx=4 fill='%s'><title>%s %s: %.0f ms</title></rect>"
                         "<text class=val x=%.1f y=%.1f text-anchor=middle>%.0f</text>"
                         % (x, 20 + plot_h - h, bar_w - 2, max(h, 1), BACKEND_COLORS[n], n, metric, value,
                            x + (bar_w - 2) / 2, 16 + plot_h - h, value))
    parts.append("</svg>")
    return "".join(parts)


def svg_ecdf(walls_by_backend, names):
    width, height, left, bottom = 640, 280, 50, 30
    plot_h = height - bottom - 20
    top = max([max(w) for w in walls_by_backend.values() if w] + [1])
    parts = ["<svg viewBox='0 0 %d %d' role=img aria-label='Latency distribution'>" % (width, height)]
    for tick in range(5):
        y = 20 + plot_h - plot_h * tick / 4
        parts.append("<line class=grid x1=%d x2=%d y1=%.1f y2=%.1f /><text class=axis x=%d y=%.1f text-anchor=end>%d%%</text>"
                     % (left, width, y, y, left - 6, y + 4, tick * 25))
    for tick in range(5):
        x = left + (width - left - 70) * tick / 4
        parts.append("<text class=axis x=%.1f y=%d text-anchor=middle>%.0f ms</text>" % (x, height - 8, top * tick / 4))
    for n in names:
        walls = sorted(walls_by_backend[n])
        if not walls:
            continue
        points = " ".join("%.1f,%.1f" % (left + (width - left - 70) * w / top, 20 + plot_h - plot_h * (i + 1) / len(walls))
                          for i, w in enumerate(walls))
        end_x = left + (width - left - 70) * walls[-1] / top
        parts.append("<polyline fill=none stroke='%s' stroke-width=2 points='%s'><title>%s</title></polyline>"
                     "<text class=val x=%.1f y=%.1f>%s</text>" % (BACKEND_COLORS[n], points, n, end_x + 4, 24, n))
    parts.append("</svg>")
    return "".join(parts)


def svg_margin_histograms(margins_by_backend, names, threshold):
    panel_w, height, bottom = 210, 190, 30
    plot_h = height - bottom - 20
    total_w = panel_w * len(names)
    counts = {n: histogram(margins_by_backend[n]) for n in names}
    top = max([max(c) for c in counts.values()] + [1])
    parts = ["<svg viewBox='0 0 %d %d' role=img aria-label='Stance margin histogram per backend'>" % (total_w, height)]
    for pi, n in enumerate(names):
        ox = pi * panel_w + 24
        w = panel_w - 36
        parts.append("<text class=val x=%.1f y=14>%s (n=%d)</text>" % (ox, n, len(margins_by_backend[n])))
        for b, count in enumerate(counts[n]):
            h = plot_h * count / top
            parts.append("<rect x=%.1f y=%.1f width=%.1f height=%.1f rx=2 fill='%s'><title>%s margin %.1f-%.1f: %d</title></rect>"
                         % (ox + b * w / 10, 20 + plot_h - h, w / 10 - 2, max(h, 0.5) if count else 0, BACKEND_COLORS[n], n,
                            b / 10, (b + 1) / 10, count))
        x = ox + w * threshold
        parts.append("<line class=thresh x1=%.1f x2=%.1f y1=20 y2=%d /><text class=axis x=%.1f y=%d text-anchor=middle>%.1f</text>"
                     % (x, x, 20 + plot_h, x, height - 8, threshold))
        parts.append("<text class=axis x=%.1f y=%d>0</text><text class=axis x=%.1f y=%d text-anchor=end>1</text>"
                     % (ox, height - 8, ox + w, height - 8))
    parts.append("</svg>")
    return "".join(parts)


CSS = """
:root{--bg:#fcfcfb;--fg:#0b0b0b;--muted:#52514e;--line:#dcdbd5;--c1:#2a78d6;--c2:#eb6834;--c3:#1baf7a}
@media(prefers-color-scheme:dark){:root{--bg:#1a1a19;--fg:#f0efec;--muted:#c3c2b7;--line:#3a3a37;--c1:#3987e5;--c2:#d95926;--c3:#199e70}}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;max-width:980px;margin:0 auto;padding:16px}
h1,h2{line-height:1.2}h2{margin-top:2.2em;border-bottom:1px solid var(--line);padding-bottom:4px}
table{border-collapse:collapse;font-size:13px;width:100%}th,td{border-bottom:1px solid var(--line);padding:4px 8px;text-align:left;vertical-align:top}
.scroll{overflow-x:auto}.legend{display:flex;gap:16px;margin:8px 0;color:var(--muted)}
.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px}
svg{width:100%;height:auto}.grid{stroke:var(--line)}.axis{fill:var(--muted);font-size:11px}.val{fill:var(--fg);font-size:11px}
.thresh{stroke:var(--fg);stroke-dasharray:4 3}.muted{color:var(--muted)}pre{white-space:pre-wrap;background:var(--line);padding:8px;border-radius:4px}
details{margin:6px 0}code{font-size:13px}
"""


def build_report(meta, results, config):
    threshold = config["min_stance_confidence_permille"] / 1000
    names = [n for n in BACKEND_ORDER if n in meta]
    live = [n for n in names if meta[n]["reachable"]]
    ok = {n: [r for r in results.get(n, []) if r["error"] is None] for n in names}
    stats = {n: timing_summary(results[n]) for n in live}
    reused = {n: timing_summary(results[n], "reused_ms") for n in live}
    sections = []

    summary_rows = []
    for n in names:
        m = meta[n]
        cold = next((r["wall_ms"] for r in results.get(n, []) if r["phase"] == "cold" and r["error"] is None), None)
        models = sorted({r["model"] for r in ok.get(n, []) if r["model"]})
        summary_rows.append([n, m["url"], "yes" if m["reachable"] else "no: " + str(m["reason"]), len(results.get(n, [])),
                             ", ".join(models) or "-", m.get("revision") or "-", fmt(cold, 0, " ms")])
    sections.append("<h2>1. Summary</h2><p>%s &middot; git %s &middot; corpus %s &middot; %d requests per backend &middot; "
                    "stance clear threshold %.3f</p>%s" % (
                        meta["_run"]["date"], html.escape(meta["_run"]["sha"]), html.escape(", ".join(meta["_run"]["corpus"])),
                        meta["_run"]["size"], threshold,
                        table(["backend", "url", "reachable", "records", "model (response)", "revision (/health)", "cold"], summary_rows)))

    speed_rows = []
    for n in live:
        rs = results[n]
        warm_ok = [r for r in ok[n] if r["phase"] == "warm"]
        servers = [r["server_ms"] for r in ok[n] if r["server_ms"] is not None]
        tokens = [r["input_tokens"] for r in ok[n] if r["input_tokens"]]
        mean_tokens = statistics.fmean(tokens) if tokens else None
        per_k = stats[n]["mean"] / mean_tokens * 1000 if mean_tokens and stats[n]["mean"] else None
        cold = next((r["wall_ms"] for r in rs if r["phase"] == "cold" and r["error"] is None), None)
        handshakes = [r["handshake_ms"] for r in ok[n]]
        speed_rows.append([n, len(rs), len(ok[n]), len(rs) - len(ok[n]), fmt(cold), fmt(stats[n]["p50"]), fmt(stats[n]["p95"]),
                           fmt(stats[n]["max"]), fmt(stats[n]["mean"]),
                           fmt(statistics.fmean(handshakes) if handshakes else None), fmt(reused[n]["p50"]), fmt(reused[n]["p95"]),
                           fmt(reused[n]["max"]),
                           fmt(statistics.fmean(servers) if servers else None), fmt(mean_tokens), fmt(per_k)])
    charts = ""
    if live:
        charts = (legend(live) + "<h3>Wall time p50 / p95 / max (warm, ms, new connection per call)</h3>" + svg_grouped_bars(stats, live) +
                  "<h3>Estimated reused-connection time p50 / p95 / max (wall minus handshake, ms)</h3>" + svg_grouped_bars(reused, live) +
                  "<h3>Wall time distribution (warm, cumulative share of requests)</h3>" +
                  svg_ecdf({n: [r["wall_ms"] for r in ok[n] if r["phase"] == "warm"] for n in live}, live) +
                  "<h3>Estimated reused-connection time distribution</h3>" +
                  svg_ecdf({n: [r["reused_ms"] for r in ok[n] if r["phase"] == "warm"] for n in live}, live))
    sections.append("<h2>2. Speed</h2>" + table(
        ["backend", "n", "ok", "failed", "cold ms", "wall p50", "wall p95", "wall max", "wall mean", "handshake mean",
         "reused p50", "reused p95", "reused max", "server ms", "input tokens", "wall ms per 1k tokens"],
        speed_rows) + "<p class=muted>Warm, successful calls only. Wall is curl time_total with a fresh connection per call; handshake is "
        "time_appconnect (TCP + TLS) or time_connect for plain HTTP; reused = wall minus handshake, an estimate of what the game's "
        "kept-alive curl handle would see. Server ms is the X-Inference-Time-Ms header (Laya only).</p>" + charts)

    failure_rows = []
    for n in live:
        classes = Counter(r["error"] for r in results[n] if r["error"])
        retries = sum(r["retries"] for r in results[n])
        for c in ERROR_CLASSES:
            if classes[c]:
                example = next(r for r in results[n] if r["error"] == c)["error_detail"]
                failure_rows.append([n, c, classes[c], (example or "")[:200]])
        if not classes:
            failure_rows.append([n, "none", 0, ""])
        failure_rows.append([n, "429/529 retries", retries, ""])
    sections.append("<h2>3. Failures</h2>" + table(["backend", "class", "count", "example"], failure_rows))

    decision = "<h2>4. Decisions</h2>"
    pair_rows = []
    for i, a in enumerate(live):
        for b in live[i + 1:]:
            both = [(x, y) for x, y in zip(results[a], results[b]) if x["error"] is None and y["error"] is None]
            cells = []
            for q in QUESTION_IDS:
                hit, total = agreement([x for x, _ in both], [y for _, y in both], q)
                cells.append("%d/%d (%s)" % (hit, total, pct(hit / total if total else None)))
            pair_rows.append(["%s vs %s" % (a, b)] + cells)
    decision += "<h3>Pairwise agreement</h3>" + (table(["pair"] + list(QUESTION_IDS), pair_rows) if pair_rows else "<p>Needs two reachable backends.</p>")
    reflex_rows = []
    for n in live:
        cells = []
        for q in QUESTION_IDS:
            hit, total = reflex_agreement(ok[n], q)
            cells.append("%d/%d (%s)" % (hit, total, pct(hit / total if total else None)))
        reflex_rows.append([n] + cells)
    decision += "<h3>Agreement with the Reflex</h3>" + table(["backend"] + list(QUESTION_IDS), reflex_rows)
    margins = {n: stance_margins(ok[n]) for n in live}
    margin_rows = []
    for n in live:
        m = margins[n]
        mismatches = sum(1 for r in ok[n] for a in r["answers"].values()
                         if a["margin_reported"] is not None and abs(a["margin_reported"] - a["margin_recomputed"]) >= 0.01)
        margin_rows.append([n, fmt(statistics.fmean(m) if m else None, 3), fmt(statistics.median(m) if m else None, 3),
                            pct(clear_rate(m, threshold)), pct(game_would_use_rate(results[n], threshold)), mismatches])
    decision += "<h3>Stance margin</h3>" + table(
        ["backend", "mean margin", "median margin", "clear rate at %.1f" % threshold, "game would use", "reported vs recomputed mismatches"],
        margin_rows)
    if live:
        decision += legend(live) + svg_margin_histograms(margins, live, threshold)
    decision += "<h3>Stance versus the Reflex (rows Reflex, columns backend choice)</h3>"
    for n in live:
        counts = Counter((r["reflex"]["stance"], r["answers"]["stance"]["choice"]) for r in ok[n] if "stance" in r["answers"])
        keys = sorted({k for pair in counts for k in pair})
        decision += "<p><b>%s</b></p>" % n + table(["reflex \\ choice"] + keys, [[rk] + [counts[(rk, ck)] for ck in keys] for rk in keys])
    sections.append(decision)

    by_index = defaultdict(dict)
    for n in live:
        for r in ok[n]:
            if "stance" in r["answers"]:
                by_index[r["index"]][n] = r
    disagreements = [d for d in by_index.values() if len({r["answers"]["stance"]["choice"] for r in d.values()}) > 1][:20]
    blocks = ""
    for d in disagreements:
        first = next(iter(d.values()))
        rows = [[n, r["answers"]["stance"]["choice"], ", ".join("%s=%.2f" % (k, v) for k, v in r["answers"]["stance"]["probabilities"].items())]
                for n, r in d.items()]
        rows.append(["reflex", first["reflex"]["stance"], ""])
        blocks += "<details><summary>#%d tick %d %s</summary><pre>%s</pre>%s</details>" % (
            first["index"], first["tick"], html.escape(first["name"]), html.escape(first["request"]["state"]),
            table(["source", "stance", "probabilities"], rows))
    sections.append("<h2>5. Disagreements</h2>" + (blocks or "<p>No stance disagreements between reachable backends.</p>"))

    def observed(describe):
        return "; ".join("%s: %s" % (n, describe(results[n])) for n in live) or "-"

    def union(rows, field):
        return ", ".join(sorted({f for r in rows for f in r[field]})) or "none"

    protocol_rows = [
        ["Endpoint", "POST {host}/v1/systemone (+ /batch)", "POST https://api.typesafe.ai/v1/systemone (no batch)",
         observed(lambda rows: ", ".join("%s x%d" % (k, v) for k, v in sorted(Counter(str(r["http_status"]) for r in rows).items())))],
        ["Auth", "none", "Authorization: Bearer key", "-"],
        ["model", "optional (english)", "required: jev-latest, jev-preview or versioned; response model is versioned",
         observed(lambda rows: ", ".join(sorted({r["model"] or "none" for r in rows})))],
        ["Extra body fields", "max_len, head_max_len, task, lang, min_confidence", "none sent", "-"],
        ["Choice answer", "choice, probabilities, confidence (1-H/log k), answer_confidence, x_jev_confidence",
         "choice, probabilities, confidence ((k*max-1)/(k-1))", observed(lambda rows: union(rows, "answer_fields"))],
        ["usage", "input/output/state tokens, truncated", "input/output tokens; over-length is 400",
         observed(lambda rows: union(rows, "usage_fields"))],
        ["Server timing header", "X-Inference-Time-Ms, Server-Timing", "retry-after on 429",
         observed(lambda rows: "X-Inference-Time-Ms on %d/%d" % (sum(r["server_ms"] is not None for r in rows), len(rows)))],
        ["Limits", "512 tokens per row, state cut from the right", "64k tokens per request",
         observed(lambda rows: "truncated %d" % sum(bool(r["truncated"]) for r in rows))],
        ["Errors", "422; 503 over 16 in flight", "400/401/413/415/422/429/529; retry 429/529",
         observed(lambda rows: ", ".join(sorted({r["error"] for r in rows if r["error"]})) or "none")],
        ["Concurrency", "one inference lock", "parallel, per-org rate limits", "-"],
        ["Price", "free", "$0.042 per Mtok input", "-"]]
    sections.append("<h2>6. Protocol differences</h2>" + table(
        ["", "laya-serve 0.4.7", "TypeSafe Jev", "Observed in this run"], protocol_rows) +
        "<p><b>Game-side implication (not fixed here):</b> <code>laya_decision</code> reads <code>x_jev_confidence</code>, which Jev "
        "never sends, so every Jev Stance would fall back as low confidence. Follow-up: read <code>confidence</code> when "
        "<code>x_jev_confidence</code> is absent, send <code>model</code> and a Bearer header from laya_curl.c.</p>")

    sections.append("""<h2>7. Method and reproduce</h2><pre>build/headless --seed 1 --ticks 300 --dump-requests build/requests-seed1.jsonl
build/headless --seed 2 --ticks 300 --dump-requests build/requests-seed2.jsonl
make laya-local
uv run --python 3.12 python tools/bench_jev_vs_laya.py --corpus build/requests-seed1.jsonl --corpus build/requests-seed2.jsonl --size 60</pre>
<ul><li>Bodies are the exact JSON the game's Laya decider sends (Reflex path, no network), evenly subsampled; Jev gets the same body plus <code>model</code>.</li>
<li>Requests run sequentially (Laya serialises; the game's transport is single-worker). The first call per backend is cold and excluded from percentiles. Timeout %d ms from data/laya.json.</li>
<li>Batch was not measured: questions embed the Combatant name, Jev has no batch and the Laya research shows no gain.</li>
<li>Every request is a separate curl process, so every call pays a fresh TCP connect (and TLS for Jev). The game keeps one curl handle per worker and reuses the connection. The reused columns subtract curl's time_appconnect (time_connect for plain HTTP) from the wall time; it is an estimate, since DNS and slow-start effects are not modelled.</li>
<li>Margin is (k*max_p - 1)/(k - 1), recomputed from probabilities for every answer and compared with the reported value.</li>
<li>laya-tomhat is on the LAN, Jev is over the WAN (no server-time header). Cold is the first call per backend, shown in the summary.</li>
<li>docs.typesafe.ai migrating-to-v1.md returns 404; api.md is the v1 reference used.</li></ul>""" % config["timeout_ms"])

    return ("<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>Jev vs Laya</title><style>%s</style></head><body><h1>Jev vs Laya benchmark</h1>%s</body></html>"
            % (CSS, "".join(sections)))


def self_test():
    assert percentile([1, 2, 3, 4, 5], 50) == 3 and percentile([10], 95) == 10 and percentile([], 50) is None
    assert abs(percentile([0, 10], 95) - 9.5) < 1e-9
    assert abs(margin({"a": 0.5, "b": 0.5})) < 1e-9 and abs(margin({"a": 1.0, "b": 0.0}) - 1) < 1e-9
    assert abs(margin({"a": 0.6, "b": 0.2, "c": 0.2}) - 0.4) < 1e-9
    answer = lambda choice, m=0.5: {"choice": choice, "margin_recomputed": m}
    a = [{"answers": {"stance": answer("x")}}, {"answers": {"stance": answer("y")}}, {"answers": {}}]
    b = [{"answers": {"stance": answer("x")}}, {"answers": {"stance": answer("z")}}, {"answers": {"stance": answer("x")}}]
    assert agreement(a, b, "stance") == (1, 2)
    rows = [{"answers": {"stance": answer("x", 0.1)}, "reflex": {"stance": "x"}, "understood": True},
            {"answers": {"stance": answer("y", 0.3)}, "reflex": {"stance": "x"}, "understood": True},
            {"answers": {}, "reflex": {"stance": "x"}, "understood": False}]
    assert reflex_agreement(rows, "stance") == (1, 2)
    assert stance_margins(rows) == [0.1, 0.3]
    assert clear_rate([0.1, 0.3], 0.2) == 0.5 and clear_rate([], 0.2) is None
    assert game_would_use_rate(rows, 0.2) == 1 / 3
    assert histogram([0.0, 0.05, 0.55, 1.0]) == [2, 0, 0, 0, 0, 1, 0, 0, 0, 1]
    timing = timing_summary([{"phase": "cold", "wall_ms": 999, "error": None}] +
                            [{"phase": "warm", "wall_ms": v, "error": None} for v in (10, 20, 30)] +
                            [{"phase": "warm", "wall_ms": 500, "error": "timeout"}])
    assert timing["p50"] == 20 and timing["max"] == 30
    assert classify_error(None, 28) == "timeout" and classify_error(None, 7) == "connect"
    assert classify_error(422, None) == "http4xx" and classify_error(503, None) == "http5xx" and classify_error(200, None) is None
    assert classify_error(302, None) == "http3xx"
    assert quoted('a"b\\c\n') == '"a\\"b\\\\c\\n"'
    raw = '{"state":"caf\u00e9 \\"x\\"","questions":{}}'
    assert json.dumps(json.loads(raw), separators=(",", ":"), ensure_ascii=False) == raw
    request = {"questions": {"stance": {"criteria": {"x": "", "y": ""}}}}
    parsed = parse_answers({"answers": {"stance": {"choice": "x", "probabilities": {"x": 0.7, "y": 0.3}, "confidence": 0.4}}}, request)
    assert parsed["stance"]["margin_reported"] == 0.4 and abs(parsed["stance"]["margin_recomputed"] - 0.4) < 1e-9
    assert understood(parsed, request) and not understood({}, request)
    print("self-test ok")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--corpus", action="append")
    parser.add_argument("--size", type=int, default=60)
    parser.add_argument("--backends", help="comma list of jev, laya-tomhat, laya-local")
    parser.add_argument("--jev-model", default="jev-latest")
    parser.add_argument("--key-file")
    parser.add_argument("--concurrency", type=int, default=1)
    parser.add_argument("--out")
    parser.add_argument("--report")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

    today = date.today().isoformat()
    corpus_paths = args.corpus or [os.path.join(ROOT, "build", "requests-seed1.jsonl")]
    out_path = args.out or os.path.join(ROOT, "build", "bench-jev-vs-laya-%s.jsonl" % today)
    report_path = args.report or os.path.expanduser("~/Downloads/_reports/%s-jev-vs-laya.html" % today)
    with open(os.path.join(ROOT, "data", "laya.json")) as f:
        config = json.load(f)
    timeout_s = config["timeout_ms"] / 1000
    selected = set(args.backends.split(",")) if args.backends else None
    definitions = backend_definitions(config, selected)
    key = ""
    if any(n == "jev" for n, _ in definitions):
        try:
            key = read_key(args.key_file or default_key_path())
        except OSError as error:
            print("jev skipped: cannot read key file (%s)" % type(error).__name__)
            definitions = [d for d in definitions if d[0] != "jev"]
    records = load_corpus(corpus_paths, args.size)
    meta = {"_run": {"date": today, "sha": git_sha(), "corpus": [os.path.basename(p) for p in corpus_paths], "size": len(records)}}
    results = {}
    for name, url in definitions:
        info, reason = probe(name, url, key, timeout_s)
        meta[name] = {"url": url, "reachable": info is not None, "reason": reason, **(info or {})}
        if info is None:
            print("%s: unreachable (%s)" % (name, reason))
            continue
        results[name] = run_backend(name, url, key, args.jev_model, timeout_s, records, args.concurrency)
        walls = [r["wall_ms"] for r in results[name] if r["error"] is None]
        print("%s: %d/%d ok, median %s ms" % (name, len(walls), len(records), fmt(percentile(walls, 50))))

    jsonl = "".join(json.dumps({k: v for k, v in r.items() if k != "request"}) + "\n" for rows in results.values() for r in rows)
    report = build_report(meta, results, config)
    if key and (key in jsonl or key in report):
        print("API key leaked into output; nothing written", file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(out_path, "w") as f:
        f.write(jsonl)
    with open(report_path, "w") as f:
        f.write(report)
    print("wrote %s and %s" % (out_path, report_path))


if __name__ == "__main__":
    sys.exit(main())
