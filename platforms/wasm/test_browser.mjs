#!/usr/bin/env node

import {spawn} from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import {fileURLToPath} from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "../..");
const webRoot = path.resolve(
    process.env.WHATSCANVAS_WEB_BUILD_DIR
        ?? path.join(repositoryRoot, "out/wasm-web/platforms/wasm/web"));
const captureRoot = path.resolve(
    process.env.WHATSCANVAS_WEB_CAPTURE_DIR
        ?? path.join(repositoryRoot, "out/visual-parity/captures/web"));

const viewports = [
    {id: "landscape", width: 786, height: 377},
    {id: "portrait", width: 393, height: 759},
];
const samples = [
    {id: "t0000", time: 0.0},
    {id: "t0500", time: 0.5},
    {id: "t1250", time: 1.25},
    {id: "t2000", time: 2.0},
];

function sleep(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
}

function chromeExecutable() {
    const candidates = [
        process.env.GOOGLE_CHROME_BIN,
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/usr/bin/google-chrome",
        "/usr/bin/google-chrome-stable",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
    ].filter(Boolean);
    const executable = candidates.find((candidate) => fs.existsSync(candidate));
    if (!executable) {
        throw new Error("Chrome/Chromium was not found; set GOOGLE_CHROME_BIN");
    }
    return executable;
}

function contentType(filePath) {
    return new Map([
        [".html", "text/html; charset=utf-8"],
        [".js", "text/javascript; charset=utf-8"],
        [".wasm", "application/wasm"],
        [".data", "application/octet-stream"],
        [".json", "application/json; charset=utf-8"],
    ]).get(path.extname(filePath)) ?? "application/octet-stream";
}

async function startServer() {
    const server = http.createServer((request, response) => {
        const requestPath = decodeURIComponent(
            new URL(request.url ?? "/", "http://127.0.0.1").pathname);
        const relativePath = requestPath === "/" ? "index.html" : requestPath.slice(1);
        const filePath = path.resolve(webRoot, relativePath);
        if (!filePath.startsWith(`${webRoot}${path.sep}`) || !fs.existsSync(filePath)
            || !fs.statSync(filePath).isFile()) {
            response.writeHead(404).end("Not found");
            return;
        }
        response.writeHead(200, {
            "Content-Type": contentType(filePath),
            "Cache-Control": "no-store",
            "Cross-Origin-Opener-Policy": "same-origin",
        });
        fs.createReadStream(filePath).pipe(response);
    });
    await new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", resolve);
    });
    return server;
}

class DevToolsSession {
    constructor(url) {
        this.socket = new WebSocket(url);
        this.pending = new Map();
        this.listeners = new Map();
        this.nextId = 1;
    }

    async open() {
        await new Promise((resolve, reject) => {
            this.socket.addEventListener("open", resolve, {once: true});
            this.socket.addEventListener("error", reject, {once: true});
        });
        this.socket.addEventListener("message", (event) => {
            const message = JSON.parse(event.data);
            if (message.id) {
                const pending = this.pending.get(message.id);
                if (!pending) return;
                this.pending.delete(message.id);
                if (message.error) {
                    pending.reject(new Error(`${pending.method}: ${message.error.message}`));
                } else {
                    pending.resolve(message.result ?? {});
                }
                return;
            }
            for (const listener of this.listeners.get(message.method) ?? []) {
                listener(message.params ?? {});
            }
        });
    }

    send(method, params = {}) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, {resolve, reject, method});
            this.socket.send(JSON.stringify({id, method, params}));
        });
    }

    on(method, listener) {
        const listeners = this.listeners.get(method) ?? [];
        listeners.push(listener);
        this.listeners.set(method, listeners);
    }

    close() {
        this.socket.close();
    }
}

async function waitForFile(filePath, timeoutMilliseconds = 15000) {
    const deadline = Date.now() + timeoutMilliseconds;
    while (Date.now() < deadline) {
        if (fs.existsSync(filePath) && fs.statSync(filePath).size > 0) return;
        await sleep(50);
    }
    throw new Error(`Timed out waiting for ${path.basename(filePath)}`);
}

async function waitForState(session, predicate, description,
                            timeoutMilliseconds = 15000) {
    const deadline = Date.now() + timeoutMilliseconds;
    let state = null;
    while (Date.now() < deadline) {
        const result = await session.send("Runtime.evaluate", {
            expression: "JSON.stringify(window.whatsCanvasDemo || null)",
            returnByValue: true,
        });
        const value = result.result?.value;
        state = value ? JSON.parse(value) : null;
        if (predicate(state)) return state;
        await sleep(40);
    }
    throw new Error(`Timed out waiting for ${description}; last state=${JSON.stringify(state)}`);
}

async function evaluate(session, expression) {
    const result = await session.send("Runtime.evaluate", {
        expression,
        awaitPromise: true,
        returnByValue: true,
    });
    if (result.exceptionDetails) {
        throw new Error(result.exceptionDetails.text ?? "JavaScript evaluation failed");
    }
    return result.result?.value;
}

async function navigate(session, baseUrl, viewport, sample) {
    await session.send("Emulation.setDeviceMetricsOverride", {
        width: viewport.width,
        height: viewport.height,
        deviceScaleFactor: 1,
        mobile: false,
    });
    await session.send("Page.navigate", {
        url: `${baseUrl}/?time=${sample.time}&dpr=2`,
    });
    const state = await waitForState(
        session, (candidate) => candidate?.ready === true,
        `${viewport.id}/${sample.id} readiness`);
    assert(state.logicalWidth === viewport.width
           && state.logicalHeight === viewport.height,
           `${viewport.id}: logical viewport mismatch: ${JSON.stringify(state)}`);
    assert(state.physicalWidth === viewport.width * 2
           && state.physicalHeight === viewport.height * 2
           && state.dpr === 2,
           `${viewport.id}: DPR drawing buffer mismatch: ${JSON.stringify(state)}`);
    return state;
}

async function capture(session, viewport, sample) {
    const result = await session.send("Page.captureScreenshot", {
        format: "png",
        fromSurface: true,
        captureBeyondViewport: false,
    });
    const directory = path.join(
        captureRoot, "feature_showcase", viewport.id);
    fs.mkdirSync(directory, {recursive: true});
    const imagePath = path.join(directory, `${sample.id}.png`);
    fs.writeFileSync(imagePath, Buffer.from(result.data, "base64"));
    const metadata = {
        schema_version: 1,
        scene_id: "feature_showcase",
        viewport_id: viewport.id,
        sample_id: sample.id,
        content_rect_pixels: [0, 0, viewport.width, viewport.height],
        device_pixel_ratio: 2,
    };
    fs.writeFileSync(
        path.join(directory, `${sample.id}.json`),
        `${JSON.stringify(metadata, null, 2)}\n`);
}

async function run() {
    assert(fs.existsSync(path.join(webRoot, "index.html")),
           `Web build is missing under ${webRoot}; run platforms/wasm/build.sh`);
    const server = await startServer();
    const address = server.address();
    assert(address && typeof address !== "string", "Static server did not bind");
    const baseUrl = `http://127.0.0.1:${address.port}`;
    const profileDirectory = fs.mkdtempSync(
        path.join(os.tmpdir(), "whatscanvas-web-test-"));
    const chromeArguments = [
        "--headless=new",
        "--no-sandbox",
        "--disable-dev-shm-usage",
        "--disable-gpu-sandbox",
        "--enable-webgl",
        "--ignore-gpu-blocklist",
        "--hide-scrollbars",
        "--remote-debugging-port=0",
        `--user-data-dir=${profileDirectory}`,
        "about:blank",
    ];
    if (process.platform === "darwin") chromeArguments.unshift("--use-angle=metal");
    const chrome = spawn(chromeExecutable(), chromeArguments, {
        stdio: ["ignore", "ignore", "pipe"],
    });
    let chromeLog = "";
    chrome.stderr.setEncoding("utf8");
    chrome.stderr.on("data", (chunk) => { chromeLog += chunk; });

    let session;
    try {
        const portFile = path.join(profileDirectory, "DevToolsActivePort");
        await waitForFile(portFile);
        const [port] = fs.readFileSync(portFile, "utf8").split(/\r?\n/);
        let targets = [];
        const deadline = Date.now() + 10000;
        while (Date.now() < deadline) {
            const response = await fetch(`http://127.0.0.1:${port}/json/list`);
            targets = await response.json();
            if (targets.some((target) => target.type === "page")) break;
            await sleep(50);
        }
        const target = targets.find((candidate) => candidate.type === "page");
        assert(target?.webSocketDebuggerUrl, "Chrome page target was not created");
        session = new DevToolsSession(target.webSocketDebuggerUrl);
        await session.open();

        const browserErrors = [];
        session.on("Runtime.exceptionThrown", (event) => {
            browserErrors.push(event.exceptionDetails?.text ?? "Runtime exception");
        });
        session.on("Log.entryAdded", (event) => {
            if (event.entry?.level === "error") browserErrors.push(event.entry.text);
        });
        await session.send("Page.enable");
        await session.send("Runtime.enable");
        await session.send("Log.enable");
        await session.send("Network.enable");
        await session.send("Network.setCacheDisabled", {cacheDisabled: true});

        let lastState;
        for (const viewport of viewports) {
            for (const sample of samples) {
                lastState = await navigate(session, baseUrl, viewport, sample);
                await capture(session, viewport, sample);
            }
        }

        const activeViewport = viewports[1];
        const activeSample = samples[2];
        lastState = await navigate(session, baseUrl, activeViewport, activeSample);
        const contextTestStarted = await evaluate(
            session, "window.whatsCanvasDemo.loseAndRestoreContext(300)");
        assert(contextTestStarted === true, "WEBGL_lose_context is unavailable");
        await waitForState(session, (state) => state?.state === "context-lost",
                           "WebGL context loss");
        lastState = await waitForState(
            session, (state) => state?.ready === true
                && Number.isFinite(state.frameIndex),
            "WebGL context restoration");

        const beforeFreeze = lastState.frameIndex;
        assert(await evaluate(
            session, "window.whatsCanvasDemo.setTestVisibility(true)") === true,
            "Page visibility test hook failed");
        await sleep(1200);
        const whileHidden = await waitForState(
            session, (state) => state?.ready === true,
            "hidden page state");
        assert(whileHidden.frameIndex === beforeFreeze,
               "Animation advanced while the page was hidden");
        assert(await evaluate(
            session, "window.whatsCanvasDemo.setTestVisibility(false)") === true,
            "Page visibility restore hook failed");
        const afterResume = await waitForState(
            session, (state) => state?.ready === true
                && state.frameIndex > beforeFreeze,
            "background resume");

        await session.send("Page.reload", {ignoreCache: true});
        lastState = await waitForState(
            session, (state) => state?.ready === true && state.frameIndex > 0,
            "cold reload");
        lastState = await waitForState(
            session, (state) => Number.isFinite(state?.fps) && state.fps >= 55,
            "display-rate frame pacing", 20000);
        assert(lastState.fps <= 165,
               `Unexpected requestAnimationFrame rate: ${lastState.fps}`);

        const severeChromeErrors = browserErrors.filter((message) =>
            /GL_INVALID|RuntimeError|Aborted|WebGL.*(?:error|invalid)/i.test(message));
        const severeProcessErrors = chromeLog.split(/\r?\n/).filter((line) =>
            /GL_INVALID|RuntimeError|Aborted|ERROR.*WebGL/i.test(line));
        assert(severeChromeErrors.length === 0 && severeProcessErrors.length === 0,
               `Browser rendering errors:\n${[...severeChromeErrors, ...severeProcessErrors].join("\n")}`);

        console.log(`WEB_BROWSER_SMOKE status=PASS fps=${lastState.fps.toFixed(1)}`
            + ` captures=${viewports.length * samples.length}`
            + " dpr=2 resize=PASS background=PASS context_restore=PASS cold_reload=PASS");
    } finally {
        session?.close();
        chrome.kill("SIGTERM");
        server.close();
    }
}

run().catch((error) => {
    console.error(`WEB_BROWSER_SMOKE status=FAIL reason=${error.message}`);
    process.exitCode = 1;
});
