const { chromium } = require('playwright');
const fs = require('fs');
const shots = JSON.parse(fs.readFileSync(process.argv[2]));
const W = +process.argv[3] || 1920, H = +process.argv[4] || 1080;
(async () => {
  const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome', args: ['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist','--allow-file-access-from-files'] });
  const p = await b.newPage({ viewport: { width: W, height: H } });
  p.on('console', m => console.log('[page]', m.text().slice(0, 800)));
  p.on('pageerror', e => console.log('[err]', String(e).slice(0, 3000)));
  await p.goto('http://127.0.0.1:8123/viewer.html');
  const n = await p.evaluate(async ([w, h]) => await window.init(w, h, ['out/kandam_district.glb','out/kandam_interiors.glb']), [W, H]);
  console.log('prims', n);
  for (const s of shots) {
    const t0 = Date.now();
    const url = await p.evaluate(o => window.shot(o), s);
    fs.writeFileSync(s.file, Buffer.from(url.split(',')[1], 'base64'));
    console.log('shot', s.file, (Date.now() - t0) + 'ms');
  }
  await b.close();
})().catch(e => { console.log('FAIL', String(e).slice(0, 4000)); process.exit(1); });
