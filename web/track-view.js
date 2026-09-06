import * as THREE from "./vendor/three.module.min.js";

const canvas = document.getElementById("track-canvas");
const viewport = document.getElementById("three-d-viewport");
const gridScale = document.getElementById("track-grid-scale");
const webglContext = canvas.getContext("webgl2", { antialias: true });

if (!webglContext) {
  const fallback = document.createElement("p");
  fallback.className = "track-fallback";
  fallback.textContent = "3D track unavailable · WebGL2 required";
  viewport.appendChild(fallback);
  canvas.hidden = true;
} else {
  startTrackView(webglContext);
}

function startTrackView(context) {
  const TRAIL_LIMIT = 4096;
  const TRAIL_POINT_DISTANCE_M = 1;
  const CORRECTION_DURATION_MS = 500;
  const MAX_PREDICTION_MS = 1500;
  const CAMERA_DIRECTION = new THREE.Vector3(0, 0.72, -1).normalize();
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x020202);
  scene.add(new THREE.AmbientLight(0xffffff, 0.65));
  const markerLight = new THREE.DirectionalLight(0xffffff, 1.15);
  markerLight.position.set(-1, 2, -1);
  scene.add(markerLight);

  const renderer = new THREE.WebGLRenderer({ canvas, context, antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));

  const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 1000000);
  const cameraTarget = new THREE.Vector3(0, 50, 50);
  camera.position.set(500, 420, -500);
  camera.lookAt(cameraTarget);

  const markerGeometry = new THREE.SphereGeometry(1, 18, 12);
  const turret = createMarker(0x55d98a);
  const launch = createMarker(0xf4b642);
  const rocket = createMarker(0x35d7d2);
  turret.position.set(0, 0, 0);
  launch.visible = false;
  rocket.visible = false;

  let ground = null;
  let minorGrid = null;
  let majorGrid = null;
  let gridStepM = 0;
  let latestFix = null;
  let latestVelocity = new THREE.Vector3();
  let receivedAtMs = 0;
  let correctionOffset = new THREE.Vector3();
  let correctionStartedAtMs = 0;
  let previousElapsedS = null;
  let trailPoints = [];

  const trail = new THREE.Line(
    new THREE.BufferGeometry(),
    new THREE.LineBasicMaterial({ color: 0x35d7d2, transparent: true, opacity: 0.72 }),
  );
  scene.add(trail);

  const liveSegmentPositions = new Float32Array(6);
  const liveSegmentGeometry = new THREE.BufferGeometry();
  liveSegmentGeometry.setAttribute(
    "position",
    new THREE.BufferAttribute(liveSegmentPositions, 3),
  );
  const liveSegment = new THREE.Line(
    liveSegmentGeometry,
    new THREE.LineBasicMaterial({ color: 0x35d7d2, transparent: true, opacity: 0.34 }),
  );
  liveSegment.visible = false;
  scene.add(liveSegment);

  rebuildGround(50);

  function createMarker(color) {
    const marker = new THREE.Mesh(
      markerGeometry,
      new THREE.MeshPhongMaterial({
        color,
        emissive: color,
        emissiveIntensity: 0.16,
        shininess: 48,
      }),
    );
    scene.add(marker);
    return marker;
  }

  function telemetryPosition(telemetry) {
    const east = Number(telemetry.rocket_east_m);
    const north = Number(telemetry.rocket_north_m);
    const up = Number(telemetry.rocket_up_m);
    if (![east, north, up].every(Number.isFinite)) {
      return null;
    }
    return new THREE.Vector3(east, up, north);
  }

  function telemetryVelocity(telemetry) {
    const horizontalSpeed = Number(telemetry.horizontal_speed_m_s);
    const verticalSpeed = Number(telemetry.vertical_speed_m_s);
    const heading = THREE.MathUtils.degToRad(Number(telemetry.heading_deg));
    if (![horizontalSpeed, verticalSpeed, heading].every(Number.isFinite)) {
      return new THREE.Vector3();
    }
    return new THREE.Vector3(
      horizontalSpeed * Math.sin(heading),
      verticalSpeed,
      horizontalSpeed * Math.cos(heading),
    );
  }

  function predictedPosition(nowMs) {
    if (!latestFix) {
      return null;
    }
    const predictionMs = Math.min(Math.max(nowMs - receivedAtMs, 0), MAX_PREDICTION_MS);
    const position = latestFix.clone().addScaledVector(latestVelocity, predictionMs / 1000);
    const correctionProgress = THREE.MathUtils.clamp(
      (nowMs - correctionStartedAtMs) / CORRECTION_DURATION_MS,
      0,
      1,
    );
    return position.addScaledVector(correctionOffset, 1 - correctionProgress);
  }

  function resetFlight() {
    latestFix = null;
    correctionOffset.set(0, 0, 0);
    previousElapsedS = null;
    trailPoints = [];
    trail.geometry.dispose();
    trail.geometry = new THREE.BufferGeometry();
    launch.visible = false;
    rocket.visible = false;
    liveSegment.visible = false;
  }

  function appendTrailPoint(position) {
    const previous = trailPoints.at(-1);
    if (previous && previous.distanceTo(position) < TRAIL_POINT_DISTANCE_M) {
      return;
    }
    trailPoints.push(position.clone());
    if (trailPoints.length > TRAIL_LIMIT) {
      trailPoints.shift();
    }
    trail.geometry.dispose();
    trail.geometry = new THREE.BufferGeometry().setFromPoints(trailPoints);
  }

  function acceptTelemetry(telemetry) {
    const position = telemetryPosition(telemetry);
    const elapsedS = Number(telemetry.elapsed_s);
    if (!position || !Number.isFinite(elapsedS)) {
      return;
    }

    if (previousElapsedS !== null && elapsedS < previousElapsedS) {
      resetFlight();
    }

    const nowMs = performance.now();
    const previousDisplayPosition = predictedPosition(nowMs);
    if (!launch.visible) {
      launch.position.copy(position);
      launch.visible = true;
    }

    latestFix = position;
    latestVelocity = telemetryVelocity(telemetry);
    receivedAtMs = nowMs;
    correctionStartedAtMs = nowMs;
    correctionOffset = previousDisplayPosition
      ? previousDisplayPosition.sub(position)
      : new THREE.Vector3();
    previousElapsedS = elapsedS;
    rocket.position.copy(position);
    rocket.visible = true;
    appendTrailPoint(position);
    updateGroundForTrack();
  }

  function niceGridStep(value) {
    const magnitude = 10 ** Math.floor(Math.log10(Math.max(value, 1)));
    const normalized = value / magnitude;
    const factor = normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10;
    return factor * magnitude;
  }

  function updateGroundForTrack() {
    let maximumHorizontalM = 500;
    for (const point of trailPoints) {
      maximumHorizontalM = Math.max(
        maximumHorizontalM,
        Math.abs(point.x),
        Math.abs(point.z),
      );
    }
    if (launch.visible) {
      maximumHorizontalM = Math.max(
        maximumHorizontalM,
        Math.abs(launch.position.x),
        Math.abs(launch.position.z),
      );
    }
    rebuildGround(niceGridStep((maximumHorizontalM * 2.4) / 40));
  }

  function rebuildGround(nextGridStepM) {
    if (nextGridStepM === gridStepM) {
      return;
    }
    gridStepM = nextGridStepM;
    const groundSpanM = gridStepM * 40;
    for (const object of [ground, minorGrid, majorGrid]) {
      if (object) {
        scene.remove(object);
        object.geometry.dispose();
        object.material.dispose();
      }
    }

    ground = new THREE.Mesh(
      new THREE.PlaneGeometry(groundSpanM, groundSpanM),
      new THREE.MeshBasicMaterial({ color: 0x030503, side: THREE.DoubleSide }),
    );
    ground.rotation.x = -Math.PI / 2;
    ground.position.y = -0.04;
    scene.add(ground);

    minorGrid = new THREE.GridHelper(groundSpanM, 40, 0x1d211f, 0x1d211f);
    minorGrid.position.y = -0.02;
    scene.add(minorGrid);

    majorGrid = new THREE.GridHelper(groundSpanM, 8, 0x4d544f, 0x343936);
    scene.add(majorGrid);
    gridScale.textContent = `Grid: ${formatMeters(gridStepM)}`;
  }

  function formatMeters(value) {
    return value >= 1000 ? `${value / 1000} km` : `${value} m`;
  }

  function updateRocket(nowMs) {
    const position = predictedPosition(nowMs);
    if (!position) {
      return;
    }
    rocket.position.copy(position);

    const lastTrailPoint = trailPoints.at(-1);
    if (lastTrailPoint) {
      liveSegmentPositions.set([
        lastTrailPoint.x,
        lastTrailPoint.y,
        lastTrailPoint.z,
        position.x,
        position.y,
        position.z,
      ]);
      liveSegmentGeometry.attributes.position.needsUpdate = true;
      liveSegmentGeometry.computeBoundingSphere();
      liveSegment.visible = lastTrailPoint.distanceTo(position) > 0.1;
    }
  }

  function sceneBounds() {
    const points = [turret.position];
    if (launch.visible) {
      points.push(launch.position);
    }
    if (rocket.visible) {
      points.push(rocket.position);
    }
    points.push(...trailPoints);
    const box = new THREE.Box3().setFromPoints(points);
    box.expandByScalar(50);
    return box;
  }

  function updateCamera() {
    const bounds = sceneBounds();
    const desiredTarget = bounds.getCenter(new THREE.Vector3());
    const size = bounds.getSize(new THREE.Vector3());
    const radius = Math.max(size.length() / 2, 220);
    const verticalFov = THREE.MathUtils.degToRad(camera.fov);
    const aspectPenalty = camera.aspect < 1 ? 1 / camera.aspect : 1;
    const distance = radius * aspectPenalty / Math.sin(verticalFov / 2) * 1.12;
    const desiredPosition = desiredTarget.clone().addScaledVector(CAMERA_DIRECTION, distance);
    cameraTarget.lerp(desiredTarget, 0.045);
    camera.position.lerp(desiredPosition, 0.045);
    camera.near = Math.max(distance / 10000, 0.1);
    camera.far = Math.max(distance * 8, 10000);
    camera.updateProjectionMatrix();
    camera.lookAt(cameraTarget);
  }

  function updateMarkerScale() {
    const height = Math.max(viewport.clientHeight, 1);
    const distance = camera.position.distanceTo(cameraTarget);
    const worldUnitsPerPixel =
      (2 * distance * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2))) / height;
    const radius = worldUnitsPerPixel * 4.5;
    turret.scale.setScalar(radius * 0.8);
    launch.scale.setScalar(radius * 0.8);
    rocket.scale.setScalar(radius);
  }

  function resizeRenderer() {
    const width = Math.max(viewport.clientWidth, 1);
    const height = Math.max(viewport.clientHeight, 1);
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }

  function render(nowMs) {
    updateRocket(nowMs);
    updateCamera();
    updateMarkerScale();
    renderer.render(scene, camera);
    requestAnimationFrame(render);
  }

  window.addEventListener("rocket-telemetry", (event) => acceptTelemetry(event.detail));
  if (window.latestRocketTelemetry) {
    acceptTelemetry(window.latestRocketTelemetry);
  }
  new ResizeObserver(resizeRenderer).observe(viewport);
  resizeRenderer();
  requestAnimationFrame(render);
}
