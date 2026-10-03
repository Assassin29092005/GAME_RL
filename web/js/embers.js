// HELLWALKER site - the ember and ash field behind every page (one canvas, no libraries).
// Embers rise and flicker, ash flakes drift down; a few dozen particles, paused while the tab is hidden, and a still
// frame only when the visitor prefers reduced motion.

export function startEmbers(canvas) {
	if (!canvas || !canvas.getContext) return;
	const ctx = canvas.getContext("2d");
	const still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
	let w = 0, h = 0, dpr = 1, parts = [], raf = 0, last = 0;

	const rand = (a, b) => a + Math.random() * (b - a);
	function spawn(p, anywhere) {
		const ember = Math.random() < 0.72;
		p.ember = ember;
		p.x = rand(0, w);
		p.y = anywhere ? rand(0, h) : ember ? h + rand(4, 40) : rand(-40, -4);
		p.r = ember ? rand(0.6, 2.2) : rand(0.8, 2.6);
		p.vy = ember ? -rand(14, 46) : rand(6, 18);
		p.vx = rand(-8, 8);
		p.phase = rand(0, Math.PI * 2);
		p.wob = rand(0.4, 1.6);
		p.life = 0;
		p.maxLife = ember ? rand(6, 16) : rand(10, 22);
		p.hue = ember ? rand(12, 38) : 30;
		return p;
	}

	function resize() {
		dpr = Math.min(window.devicePixelRatio || 1, 2);
		w = window.innerWidth;
		h = window.innerHeight;
		canvas.width = Math.round(w * dpr);
		canvas.height = Math.round(h * dpr);
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		const want = Math.round(Math.min(90, Math.max(28, (w * h) / 16000)));
		while (parts.length < want) parts.push(spawn({}, true));
		parts.length = want;
	}

	function draw(dt) {
		ctx.clearRect(0, 0, w, h);
		ctx.globalCompositeOperation = "lighter";
		for (const p of parts) {
			p.life += dt;
			p.phase += dt * p.wob;
			p.x += (p.vx + Math.sin(p.phase) * 10) * dt;
			p.y += p.vy * dt;
			if (p.life > p.maxLife || p.y < -50 || p.y > h + 50 || p.x < -50 || p.x > w + 50) spawn(p, false);
			const fade = Math.min(1, p.life / 1.2) * Math.min(1, (p.maxLife - p.life) / 2);
			if (p.ember) {
				const flicker = 0.65 + 0.35 * Math.sin(p.phase * 3.1);
				const a = 0.85 * fade * flicker;
				const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, p.r * 5);
				g.addColorStop(0, `hsla(${p.hue + 12}, 100%, 75%, ${a})`);
				g.addColorStop(0.35, `hsla(${p.hue}, 100%, 55%, ${a * 0.45})`);
				g.addColorStop(1, `hsla(${p.hue}, 100%, 45%, 0)`);
				ctx.fillStyle = g;
				ctx.beginPath();
				ctx.arc(p.x, p.y, p.r * 5, 0, Math.PI * 2);
				ctx.fill();
			} else {
				ctx.globalCompositeOperation = "source-over";
				ctx.fillStyle = `rgba(190, 180, 168, ${0.18 * fade})`;
				ctx.beginPath();
				ctx.ellipse(p.x, p.y, p.r, p.r * 0.6, p.phase, 0, Math.PI * 2);
				ctx.fill();
				ctx.globalCompositeOperation = "lighter";
			}
		}
		ctx.globalCompositeOperation = "source-over";
	}

	function frame(t) {
		const dt = Math.min(0.05, (t - (last || t)) / 1000);
		last = t;
		draw(dt);
		raf = requestAnimationFrame(frame);
	}

	resize();
	window.addEventListener("resize", () => {
		resize();
		if (still) draw(0);
	});
	if (still) {
		draw(0);
		return;
	}
	document.addEventListener("visibilitychange", () => {
		if (document.hidden) {
			cancelAnimationFrame(raf);
			raf = 0;
		} else if (!raf) {
			last = 0;
			raf = requestAnimationFrame(frame);
		}
	});
	raf = requestAnimationFrame(frame);
}
