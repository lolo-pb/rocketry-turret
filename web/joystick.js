const connect = document.getElementById("connect-joystick");
const input = document.getElementById("joystick-input");
const status = document.getElementById("joystick-status");
let mode;
window.addEventListener("rocket-telemetry", (event) => {
  mode = event.detail.controller_connected ? event.detail.state : null;
});

if (!navigator.serial) {
  connect.disabled = true;
  status.textContent = "Web Serial requires a compatible browser on HTTPS or localhost.";
}

connect.addEventListener("click", async () => {
  connect.disabled = true;
  let reader;
  let port;
  let opened = false;
  let previousButton;
  let staleTimer;
  let sending = false;
  let sendError;
  let lastSent = -Infinity;
  let lastPress = 0;
  let axisRequest;
  const requests = new AbortController();

  async function sendAxes(x, y) {
    sending = true;
    lastSent = performance.now();
    try {
      const reply = await fetch(`/api/joystick?x=${x}&y=${y}`, {
        method: "POST",
        signal: requests.signal,
      });
      if (!reply.ok) throw new Error((await reply.json()).detail || "Joystick input rejected");
    } catch (error) {
      if (!requests.signal.aborted) {
        sendError = error;
        await reader.cancel();
      }
    } finally {
      sending = false;
    }
  }

  const clearReadout = () => {
    input.textContent = "X — · Y — · Button —";
    previousButton = undefined;
  };
  clearReadout();
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    opened = true;
    reader = port.readable.getReader();
    status.textContent = "Waiting for joystick data";
    const decoder = new TextDecoder();
    let partial = "";
    while (true) {
      const { value, done } = await reader.read();
      if (sendError) throw sendError;
      if (done) {
        status.textContent = "Joystick disconnected";
        break;
      }
      const lines = (partial + decoder.decode(value, { stream: true })).split("\n");
      partial = lines.pop();
      let latest;
      for (const line of lines) {
        const match = /^(\d{1,4}),(\d{1,4}),([01])$/.exec(line.trim());
        if (!match) continue;
        const [, x, y, pressed] = match.map(Number);
        if (x > 1023 || y > 1023) continue;
        input.textContent = `X ${x} · Y ${y} · Button ${pressed}`;
        if (previousButton === 0 && pressed === 1) {
          const now = performance.now();
          let url;
          if (mode === "MANUAL_ACQUISITION") {
            url = "/api/arm";
            lastPress = 0;
          } else if (mode === "TRACKING") {
            if (lastPress && now - lastPress <= 600) url = "/api/start-manual";
            lastPress = url ? 0 : now;
          }
          if (url) {
            await axisRequest;
            if (sendError) throw sendError;
            const reply = await fetch(url, { method: "POST" });
            const result = await reply.json();
            if (!reply.ok) throw new Error(result.detail || "Controller rejected the command");
            mode = result.state;
          }
        }
        previousButton = pressed;
        latest = { x, y };
        status.textContent = "Press the stick to start antenna tracking";
        clearTimeout(staleTimer);
        staleTimer = setTimeout(() => {
          clearReadout();
          status.textContent = "Waiting for joystick data";
        }, 250);
      }
      if (latest && mode === "MANUAL_ACQUISITION" && !sending && performance.now() - lastSent >= 40) {
        axisRequest = sendAxes(latest.x, latest.y);
      }
    }
    clearReadout();
  } catch (error) {
    clearReadout();
    status.textContent = error.message;
  } finally {
    clearTimeout(staleTimer);
    requests.abort();
    reader?.releaseLock();
    try {
      if (opened) await port.close();
    } finally {
      connect.disabled = false;
    }
  }
});
