import { chromium, firefox, webkit } from '/usr/local/lib/node_modules/@playwright/cli/node_modules/playwright-core/index.mjs';
import * as path from 'path';
import * as fs from 'fs';
import * as crypto from 'crypto';
import { execFileSync } from 'child_process';

const webDir = path.resolve(new URL('.', import.meta.url).pathname);
const sample = path.join(webDir, 'test_sample.jpg');
const artifactsRoot = path.resolve(process.env.SG_ARTIFACTS || path.join(webDir, 'artifacts'));
const artifacts = path.join(artifactsRoot, `run-${new Date().toISOString().replace(/[:.]/g, '-')}`);
fs.mkdirSync(artifacts, { recursive: true });
const stdoutPath = path.join(artifacts, 'stdout.log');
const nativeStdoutWrite = process.stdout.write.bind(process.stdout);
process.stdout.write = (chunk, ...rest) => { fs.appendFileSync(stdoutPath, chunk); return nativeStdoutWrite(chunk, ...rest); };
const argv = process.argv.slice(2);
const arg = n => { const i = argv.indexOf(n); return i < 0 ? null : argv[i + 1]; };
const url = arg('--url') || process.env.SG_URL || 'http://127.0.0.1:8092/calib.html';
const browserName = arg('--browser') || process.env.SG_BROWSER || 'chromium';
const type = { chromium, firefox, webkit }[browserName];
if (!type) throw new Error(`unsupported browser: ${browserName}`);
const browser = await type.launch({ headless: true, ...(process.env.SG_BROWSER_PATH ? { executablePath: process.env.SG_BROWSER_PATH } : {}), args: ['--no-sandbox'] });
const ctx = await browser.newContext({ viewport: { width: 1440, height: 1800 }, deviceScaleFactor: 1, acceptDownloads: true });
const page = await ctx.newPage();
const logs = [], hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
page.on('console', m => logs.push(`[${m.type()}] ${m.text()}`)); page.on('pageerror', e => logs.push(`[pageerror] ${e.message}`));
let n = 0;
async function cp(name, fn) { n++; await fn(); console.log(`[CHECKPOINT ${String(n).padStart(2, '0')}/25] ${name}`); }
async function drag(id, a, b) { const q = await page.locator(id).boundingBox(); if (!q) throw Error(`${id} missing`); await page.mouse.move(q.x+a[0]*q.width,q.y+a[1]*q.height); await page.mouse.down(); await page.mouse.move(q.x+b[0]*q.width,q.y+b[1]*q.height,{steps:8}); await page.mouse.up(); await page.waitForTimeout(100); }
async function dragImageRect(a,b) { const p=await page.evaluate(([a,b])=>{const k=cv1.getBoundingClientRect().width/cv1.width;const x=img2scr1(a),y=img2scr1(b),q=cv1.getBoundingClientRect();return {x1:q.left+x.x*k,y1:q.top+x.y*k,x2:q.left+y.x*k,y2:q.top+y.y*k};},[a,b]);await page.mouse.move(p.x1,p.y1);await page.mouse.down();await page.mouse.move(p.x2,p.y2,{steps:8});await page.mouse.up();await page.waitForTimeout(100); }
async function st() { return page.evaluate(() => ({w:W,h:H,rois:S.rois.length,roiPx:S.rois.map(roiPx),profile:JSON.stringify(buildProfile(0)),params:{...S.params},disabled:$('btnExport').disabled,checks:$('checks').innerText})); }
async function frame() { await page.click('#btnReset'); await page.setInputFiles('#fileImg',sample); await page.waitForTimeout(150); await drag('#cv1',[.14,.04],[.54,.32]); await page.click('#toolAnchor'); await drag('#cv2',[.05,.12],[.21,.57]); }
async function rois(count) { const start=await page.evaluate(()=>S.rois.length); await page.click('#toolScrew'); const q=await page.locator('#cv2').boundingBox(); const cols=8, rows=Math.ceil(count/cols); for(let i=start;i<count;i++){const x=.18+(i%cols)*.64/(cols-1), y=.18+Math.floor(i/cols)*.64/Math.max(1,rows-1); await page.mouse.click(q.x+x*q.width,q.y+y*q.height);} await page.waitForTimeout(100); }
async function screenshot(name) { const p=path.join(artifacts,`${String(n).padStart(2,'0')}_${name}.png`); await page.screenshot({path:p,fullPage:true}); console.log(`[SCREENSHOT] ${p}`); return p; }
async function exportTo(name) { const [d]=await Promise.all([page.waitForEvent('download',{timeout:5000}),page.click('#btnExport')]); const p=path.join(artifacts,name); await d.saveAs(p); if(!fs.existsSync(p)||fs.statSync(p).size<100)throw Error(`empty export ${name}`); return p; }
function schema(p) { execFileSync('uv',['run','--with','jsonschema','python',path.join(webDir,'validate_schema.py'),p],{stdio:'inherit'}); }

try {
  await cp('页面可访问且无外部依赖',async()=>{await page.goto(url,{waitUntil:'domcontentloaded'});await page.waitForSelector('#cv1');});
  await cp('控件上传实物样品',async()=>{await page.setInputFiles('#fileImg',sample);await page.waitForTimeout(150);});
  await cp('真实鼠标绘制工件框',()=>dragImageRect({x:105,y:31},{x:405,y:231}));
  await cp('真实鼠标绘制方向锚点',async()=>{await page.click('#toolAnchor');await drag('#cv2',[.05,.12],[.21,.57]);});
  await cp('真实鼠标标注第1孔',async()=>{await page.click('#toolScrew');const q=await page.locator('#cv2').boundingBox();await page.mouse.click(q.x+236/300*q.width,q.y+70/200*q.height);});
  await cp('真实鼠标标注第2孔',async()=>{await page.click('#toolScrew');const q=await page.locator('#cv2').boundingBox();await page.mouse.click(q.x+65/300*q.width,q.y+130/200*q.height);if((await st()).rois!==2)throw Error('not 2 rois');});
  await cp('两孔实物离线试检精确裁决',async()=>{await page.click('#btnTrial');await page.waitForTimeout(800);const t=await page.locator('#trialOut').innerText();console.log('[TRIAL]',t.slice(0,240));if(!t.includes('DEFECT')||!t.includes('MISSING_SCREW'))throw Error('overall裁决错误');const rows=await page.locator('#trialOut tbody tr').evaluateAll(rs=>rs.map(r=>Array.from(r.querySelectorAll('td')).map(x=>x.innerText.trim())));if(rows.length!==2||!rows[0].some(x=>x==='PRESENT')||!rows[1].some(x=>x==='MISSING'))throw Error(`per-hole裁决错误 ${JSON.stringify(rows)}`);});
  await cp('两孔真实点击导出',async()=>{const s=await st();console.log('[STATE BEFORE EXPORT]',JSON.stringify(s));if(s.disabled)throw Error('export disabled: '+s.checks);await exportTo('two-hole.json');});
  await cp('两孔导出 schema 校验',()=>schema(path.join(artifacts,'two-hole.json')));
  await cp('导入回读尺寸与孔数',async()=>{await page.setInputFiles('#fileImport',path.join(artifacts,'two-hole.json'));await page.waitForTimeout(100);const s=await st();if(s.rois!==2||s.w!==300||s.h!==200)throw Error(JSON.stringify(s));});
  await cp('四孔横向导出 schema',async()=>{await frame();await drag('#cv1',[.14,.1],[.58,.32]);await rois(4);const s=await st();if(s.rois!==4||s.w===300&&s.h===200)throw Error(JSON.stringify(s));await page.click('#btnTrial');await page.waitForTimeout(300);schema(await exportTo('four-horizontal.json'));});
  await cp('四孔竖向导出 schema',async()=>{await page.click('#btnReset');await page.setInputFiles('#fileImg',sample);await page.waitForTimeout(150);await drag('#cv1',[.3,.04],[.52,.58]);await page.click('#toolAnchor');await drag('#cv2',[.05,.12],[.21,.57]);await rois(4);const s=await st();if(s.rois!==4||s.h<=s.w)throw Error(JSON.stringify(s));await page.click('#btnTrial');await page.waitForTimeout(300);schema(await exportTo('four-vertical.json'));});
  await cp('整数尺寸边界',async()=>{const s=await st();if(!Number.isInteger(s.w)||!Number.isInteger(s.h)||s.w<30||s.h<30)throw Error(JSON.stringify(s));});
  await cp('32孔真实鼠标标注并强制导出',async()=>{await page.click('#btnReset');await page.setInputFiles('#fileImg',sample);await page.waitForTimeout(150);await dragImageRect({x:105,y:31},{x:405,y:231});await page.click('#toolAnchor');await drag('#cv2',[.05,.12],[.21,.57]);await rois(32);const s=await st();if(s.rois!==32||s.disabled)throw Error(JSON.stringify(s));const p=await exportTo('thirty-two-hole.json');schema(p);});
  await cp('33孔真实鼠标点击被拒绝',async()=>{const before=(await st()).rois;await page.click('#toolScrew');const q=await page.locator('#cv2').boundingBox();await page.mouse.click(q.x+.5*q.width,q.y+.5*q.height);await page.waitForTimeout(100);if((await st()).rois!==before)throw Error('33rd accepted');if(!await page.locator('#toast').innerText().then(x=>x.includes('上限')))throw Error('missing upper-limit toast');});
  await cp('30x30边界通过导入控件',async()=>{const p=path.join(artifacts,'size-30.json'),j=JSON.parse(fs.readFileSync(path.join(artifacts,'two-hole.json')));j.profile.canonical_size=[30,30];fs.writeFileSync(p,JSON.stringify(j));await page.setInputFiles('#fileImport',p);await page.waitForTimeout(100);const s=await st();if(s.w!==30||s.h!==30)throw Error(JSON.stringify(s));});
  await cp('1280x1280边界通过导入控件',async()=>{const p=path.join(artifacts,'size-1280.json'),j=JSON.parse(fs.readFileSync(path.join(artifacts,'two-hole.json')));j.profile.canonical_size=[1280,1280];fs.writeFileSync(p,JSON.stringify(j));await page.setInputFiles('#fileImport',p);await page.waitForTimeout(100);const s=await st();if(s.w!==1280||s.h!==1280)throw Error(JSON.stringify(s));});
  await cp('非法尺寸通过导入控件拒绝',async()=>{const p=path.join(artifacts,'invalid-size-boundary.json'),j=JSON.parse(fs.readFileSync(path.join(artifacts,'two-hole.json')));j.profile.canonical_size=[1281,30];fs.writeFileSync(p,JSON.stringify(j));await page.setInputFiles('#fileImport',p);await page.waitForTimeout(100);if((await st()).w===1281)throw Error('invalid size imported');});
  await cp('极端比例真实图片框选拒绝',async()=>{await page.click('#btnReset');await page.setInputFiles('#fileImg',{name:'extreme_fixture.svg',mimeType:'image/svg+xml',buffer:fs.readFileSync(path.join(webDir,'extreme_fixture.svg'))});await page.waitForFunction(()=>src&&src.width===2000&&src.height===100,{timeout:3000});const p=await page.evaluate(()=>{const k=cv1.getBoundingClientRect().width/cv1.width;const a=img2scr1({x:10,y:10}),b=img2scr1({x:1990,y:40}),q=cv1.getBoundingClientRect();return {x1:q.left+a.x*k,y1:q.top+a.y*k,x2:q.left+b.x*k,y2:q.top+b.y*k};});await page.mouse.move(p.x1,p.y1);await page.mouse.down();await page.mouse.move(p.x2,p.y2,{steps:8});await page.mouse.up();await page.waitForTimeout(100);const s=await st();if(!/超出支持范围|非法标准尺寸/.test(s.checks))throw Error(JSON.stringify(s));});
  await cp('非法 mismatch 导入不改完整状态',async()=>{const before=await st(),p=path.join(artifacts,'invalid-mismatch.json');const j=JSON.parse(fs.readFileSync(path.join(artifacts,'two-hole.json')));j.profile.expected_screws_per_block=1;fs.writeFileSync(p,JSON.stringify(j));await page.setInputFiles('#fileImport',p);await page.waitForTimeout(100);const after=await st();if(after.profile!==before.profile||JSON.stringify(after.params)!==JSON.stringify(before.params)||after.w!==before.w||after.h!==before.h)throw Error('state changed');});
  await cp('非法尺寸导入不改完整状态',async()=>{const before=await st(),p=path.join(artifacts,'invalid-size.json');const j=JSON.parse(fs.readFileSync(path.join(artifacts,'two-hole.json')));j.profile.canonical_size=[29,200];fs.writeFileSync(p,JSON.stringify(j));await page.setInputFiles('#fileImport',p);await page.waitForTimeout(100);const after=await st();if(after.profile!==before.profile||JSON.stringify(after.params)!==JSON.stringify(before.params)||after.w!==before.w||after.h!==before.h)throw Error('state changed');});
  await cp('合法配方再次导入',async()=>{await page.setInputFiles('#fileImport',path.join(artifacts,'two-hole.json'));await page.waitForTimeout(100);if((await st()).rois!==2)throw Error('readback failed');});
  await cp('无静默截断提示',async()=>{if(/截断|同步/.test((await st()).checks))throw Error('silent truncation text');});
  await cp('控制台无 pageerror',async()=>{if(logs.some(x=>x.startsWith('[pageerror]')))throw Error(logs.join('\n'));});
  await cp('截图与原始日志',async()=>{await screenshot('final');fs.writeFileSync(path.join(artifacts,'console.log'),logs.join('\n')+'\n');});
  console.log(`[E2E] PASS ${n} checkpoints; artifacts=${artifacts}`);
  const writeManifest = () => {
    const files=fs.readdirSync(artifacts,{withFileTypes:true}).filter(x=>x.isFile()&&x.name!=='sha256.txt').map(x=>x.name);
    const sources=['calib.html','validate_schema.py','test_e2e_walkthrough.mjs','extreme_fixture.svg'];
    const manifest=[...files.map(f=>`${hash(path.join(artifacts,f))}  ${f}`),...sources.map(f=>`${hash(path.join(webDir,f))}  ../../${f}`)].join('\n')+'\n';
    fs.writeFileSync(path.join(artifacts,'sha256.txt'),manifest);
  };
  writeManifest();
} catch(e) { console.error('[E2E FATAL ERROR]',e); fs.writeFileSync(path.join(artifacts,'console.log'),logs.join('\n')+`\n[FATAL] ${e.stack}\n`); process.exitCode=1; }
finally { await browser.close(); }
