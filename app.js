'use strict';
const $=id=>document.getElementById(id);
const kinds={reel:'Instagram Reel',substack:'Substack idea'};
const localService=location.protocol==='http:' && ['127.0.0.1','localhost'].includes(location.hostname);
const dbPromise=new Promise((resolve,reject)=>{
  const req=indexedDB.open('luna-creator-postcards',1);
  req.onupgradeneeded=()=>req.result.createObjectStore('cards',{keyPath:'id'});
  req.onsuccess=()=>resolve(req.result);req.onerror=()=>reject(req.error);
});
async function store(access,fn){
  const db=await dbPromise;
  return new Promise((resolve,reject)=>{
    const tx=db.transaction('cards',access),req=fn(tx.objectStore('cards'));
    tx.oncomplete=()=>resolve(req.result);
    tx.onabort=tx.onerror=()=>reject(tx.error||Error('Could not save in this browser.'));
  });
}
const persist=card=>store('readwrite',s=>s.put(card));
const el=(tag,text)=>{const node=document.createElement(tag);node.textContent=text;return node;};
const modal=id=>{const d=$(id);if(d&&!d.open)d.showModal();};
let active=null, playingURL=null, recorder=null, recordingTimer=null, goQuiet=null;
let busy=false, muted=localStorage.getItem('luna-quiet')==='yes', utterances=[];
let services={ai:false,notion:false};
function status(s){$('status').textContent=s;}
function speech(text,replace=false){
  if(muted||!window.speechSynthesis||!text)return;
  if(replace){speechSynthesis.cancel();utterances=[];}
  const line=new SpeechSynthesisUtterance(text);
  line.lang='en-GB';line.rate=1.0;
  const voices=speechSynthesis.getVoices();
  const preferred=voices.find(v=>v.localService&&v.lang==='en-GB');
  if(preferred)line.voice=preferred;
  utterances.push(line);line.onend=line.onerror=()=>{utterances=utterances.filter(u=>u!==line);};
  speechSynthesis.speak(line);
}
function soundState(){const b=$('sound');b.setAttribute('aria-pressed',String(!muted));b.setAttribute('aria-label',muted?'Turn voice replies on':'Mute voice replies');b.textContent=muted?'Voice off':'Voice on';}
$('sound').onclick=()=>{muted=!muted;localStorage.setItem('luna-quiet',muted?'yes':'no');if(muted)window.speechSynthesis?.cancel();soundState();};
function setMode(phase,text){
  $('phase').textContent=phase;status(text);
  document.body.dataset.phase=phase.toLowerCase().replace(/\W+/g,'-');
}
function syncControls(){
  const listening=recorder?.state==='recording';
  $('mic').disabled=busy&&!listening;$('save').disabled=busy;$('kind').disabled=busy;
  $('mic').setAttribute('aria-label',listening?'Finish recording':'Record an idea');
  const label=$('mic').querySelector('[data-mic-label]');if(label)label.textContent=listening?'Finish recording':'Record an idea';else $('mic').textContent=listening?'Finish recording':'Record an idea';
  $('mic').setAttribute('aria-pressed',String(Boolean(listening)));
}
function setBusy(flag){busy=flag;syncControls();}
const api=async(path,body)=>{
  if(!localService)throw Error('Open Launch_Luna.command to connect the Loft to its companion.');
  const res=await fetch('/api/'+path,body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{});
  const json=await res.json();if(!res.ok){const err=Error(json.error||'Luna could not connect.');err.code=json.code;throw err;}return json;
};
async function checkServices(){
  try{services=await api('status');}catch{services={ai:false,notion:false};}
  $('serviceStatus').textContent=localService
    ? ['AI service '+(services.ai?'configured':'needs a key'),'Demo inbox saves to this Mac'].join(' · ')
    : 'Local preview. Open Launch_Luna.command from the project to enable spoken B-roll. Your demo inbox is local.';
  $('connectServices').disabled=!localService;
}
$('settingsOpen').onclick=()=>{modal('settings');checkServices();};
$('connectServices').onclick=async e=>{
  e.preventDefault();
  try{await api('settings',{ai:$('apiKey').value});
    $('apiKey').value='';await checkServices();
  }catch(err){$('serviceStatus').textContent=err.message;}
};
function safeLink(url){try{const u=new URL(url);return u.protocol==='https:'&&(u.hostname==='notion.so'||u.hostname.endsWith('.notion.so')||u.hostname==='notion.com'||u.hostname.endsWith('.notion.com'))?u.href:'';}catch{return '';}}
const ordinal=['First, before it’s gone:','Second:','Third:'];
function makeReply(card){return (card.shots||[]).map((s,i)=>(ordinal[i]||'')+' '+s.subject+'. '+s.direction).join(' ');}
function savedLine(card){return 'Your '+kinds[card.kind]+(card.kind==='reel'?' idea':'')+' is saved in your local demo inbox.';}
function renderShots(card){
  const box=$('shots');box.replaceChildren();
  for(const [i,shot] of (card.shots||[]).entries()){
    const row=el('li',''),info=el('div',''),btn=el('button',card.taken?.[i]?'✓':'○');
    btn.type='button';btn.setAttribute('aria-label',(card.taken?.[i]?'Unmark ':'Mark filmed ')+shot.subject);
    btn.setAttribute('aria-pressed',String(!!card.taken?.[i]));
    btn.onclick=async()=>{
      card.taken||={};card.taken[i]=!card.taken[i];await persist(card);renderShots(card);
    };
    if(i===0){const first=el('span','Film first · before it’s gone');first.className='shot-first';info.append(first);}
    info.append(el('strong',shot.subject+' · '+shot.seconds),el('p',shot.direction));
    if(card.taken?.[i])row.className='filmed';
    row.append(btn,info);box.append(row);
  }
  $('shotsTitle').hidden=!card.shots?.length;
}
function renderCard(card){
  active=card;const hasReceipt=!!safeLink(card.url);
  if(card.storage==='local-demo')document.body.dataset.receipt='local-demo';
  else delete document.body.dataset.receipt;
  $('postcard').hidden=false;
  $('currentTitle').textContent=(kinds[card.kind]||'Postcard')+(card.kind==='reel'?' idea':'');
  $('response').textContent=card.note?'“'+card.note+'”':'Luna is listening back to your voice note…';
  const when=new Date(card.date||Date.now());
  $('pcPostmark').textContent='Moon Loft · NYC · '+when.toLocaleDateString('en-GB',{day:'numeric',month:'short'});
  $('pcFiled').textContent=card.storage==='local-demo'?'Filed in demo inbox · '+(card.kind==='reel'?'Reels':'Substack'):'Waiting in your satchel';
  $('notionLink').hidden=!hasReceipt;
  if(hasReceipt)$('notionLink').href=safeLink(card.url);else $('notionLink').removeAttribute('href');
  $('retry').hidden=!!card.shots?.length;
  $('retry').textContent=(hasReceipt||card.storage==='local-demo')?'Get filming ideas':'Prepare filming ideas';
  $('replayVoice').hidden=!card.shots?.length;renderShots(card);
  const detail=$('detail');detail.replaceChildren();
  if(playingURL){URL.revokeObjectURL(playingURL);playingURL=null;}
  if(card.audio){
    playingURL=URL.createObjectURL(card.audio);
    const audio=document.createElement('audio');audio.controls=true;audio.src=playingURL;
    const heading=el('p','Your original voice note');heading.className='filing-note';detail.append(heading);
    const link=el('a','Download voice note');link.href=playingURL;
    link.download='Luna-'+card.id+(card.audio.type.includes('wav')?'.wav':card.audio.type.includes('mp4')?'.m4a':card.audio.type.includes('ogg')?'.ogg':'.webm');
    detail.append(audio,link);
  }
}
async function refresh(){
  const cards=await store('readonly',s=>s.getAll());const list=$('list');list.replaceChildren();
  if(!cards.length){list.append(el('li','Your first postcard will appear here.'));return;}
  for(const card of cards.sort((a,b)=>b.date-a.date)){
    const li=el('li',''),button=el('button',card.title||kinds[card.kind]||'Idea');
    button.append(el('small',(kinds[card.kind]||'Postcard')+' · '+(card.url?'Notion':card.storage==='local-demo'?'Demo inbox':'Local')+' · '+new Date(card.date).toLocaleDateString()));
    button.onclick=()=>{if(busy)return;$('satchel').close();modal('creator');renderCard(card);setMode(safeLink(card.url)||card.storage==='local-demo'?'Postmarked':'In your satchel',safeLink(card.url)?'Saved in Notion.':card.storage==='local-demo'?'Filed in demo inbox on this Mac.':'Saved in this browser.');};
    li.append(button);list.append(li);
  }
}
$('satchelOpen')?.addEventListener('click',()=>{modal('satchel');refresh().catch(e=>status(e.message));});
function draft(note,audio,kind,source){
  return {id:crypto.randomUUID(),date:Date.now(),kind,note,audio,source,scene:$('scene').value.trim(),title:note.trim().slice(0,70)||kinds[kind]+' voice note'};
}
function blobText(blob){
  return new Promise((resolve,reject)=>{const f=new FileReader();f.onload=()=>resolve(String(f.result).split(',')[1]);f.onerror=()=>reject(f.error);f.readAsDataURL(blob);});
}
const processing=new Map();
function deliver(card){
  if(processing.has(card.id))return processing.get(card.id);
  const job=doDelivery(card).finally(()=>processing.delete(card.id));processing.set(card.id,job);return job;
}
async function doDelivery(card){
  setBusy(true);renderCard(card);$('retry').hidden=true;
  try{
    if(card.storage!=='local-demo'){
      if(!localService){
        setMode('In your satchel','Saved in this browser. Launch the companion to file it in the demo inbox and prepare B-roll.');
        speech('Your '+kinds[card.kind]+' is saved in this browser.',true);
        $('retry').hidden=true;return;
      }
      setMode('Sending','Luna is listening back to your idea…');
      const payload={id:card.id,kind:card.kind,note:card.note,scene:card.scene||''};
      if(card.audio){payload.audio=await blobText(card.audio);payload.mime=card.audio.type;}
      const receipt=await api('postcards/save',payload);
      if(receipt.storage!=='local-demo'||receipt.id!==card.id)throw Error('Still waiting for the local inbox receipt.');
      Object.assign(card,receipt);await persist(card);renderCard(card);
      const thanks=savedLine(card);
      setMode('Postmarked',thanks+' Stored on this Mac.');speech(thanks,true);await refresh();
    }
    if(!card.shots?.length){
      status('Saved in your demo inbox. Luna is choosing your shots…');
      const result=await api('postcards/shots',{id:card.id});
      card.shots=result.shots;await persist(card);renderCard(card);
      speech('Here are three shots to film while you’re there. '+makeReply(card));
    }
    setMode('Film here now','Your shot list is ready. Tap a circle as you film.');
  }catch(e){
    const msg='Your original is in the satchel on this Mac. '+e.message;
    setMode(card.storage==='local-demo'?'Postmarked':'In your satchel',msg);
    speech(msg,card.storage!=='local-demo');
    $('retry').hidden=false;$('retry').textContent='Retry filming ideas';
  }finally{setBusy(false);refresh().catch(e=>status(e.message));}
}
async function keepAndDeliver(card){await persist(card);await refresh();await deliver(card);}
$('replayVoice').onclick=()=>{if(active)speech('Here are your three shots. '+makeReply(active),true);};
$('save').onclick=async e=>{
  e.preventDefault();if(busy)return;
  const note=$('words').value.trim();if(!note){$('words').focus();return;}
  try{await keepAndDeliver(draft(note,null,$('kind').value,'Written on this Mac'));$('words').value='';}
  catch(err){setBusy(false);status(err.message);}
};
function clearCard(){
  delete document.body.dataset.receipt;
  active=null;$('postcard').hidden=true;$('shotsTitle').hidden=true;$('currentTitle').textContent='';$('shots').replaceChildren();$('response').textContent='';
  $('detail').replaceChildren();for(const id of ['retry','notionLink','replayVoice'])$(id).hidden=true;
}
let retained=null;
async function finishLocal(card){
  retained=card;
  try{await keepAndDeliver(card);retained=null;}
  catch(err){setBusy(false);$('retry').hidden=false;$('retry').textContent='Retry local save';active=card;status('Keep this window open. '+err.message);}
}
$('retry').onclick=()=>{if(!busy){if(retained)finishLocal(retained);else if(active){
        deliver(active);
  }}};
async function record(){
  if(recorder?.state==='recording'){recorder.stop();return;}
  if(busy||retained)return;
  modal('creator');clearCard();window.speechSynthesis?.cancel();setBusy(true);
  let stream;
  try{
    if(!navigator.mediaDevices?.getUserMedia)throw Error('Microphone capture needs desktop Chrome or localhost.');
    setMode('Microphone','Allow microphone access to begin.');
    stream=await navigator.mediaDevices.getUserMedia({audio:true});
    if(!$('creator').open){stream.getTracks().forEach(t=>t.stop());setBusy(false);return;}
    const card=draft('',null,$('kind').value,'Recorded in the Loft');
    const rec=new MediaRecorder(stream),parts=[];recorder=rec;
    const started=Date.now();$('timer').textContent='0:00';
    rec.ondataavailable=e=>{if(e.data.size)parts.push(e.data);};
    rec.onerror=()=>{stream.getTracks().forEach(t=>t.stop());status('Microphone interrupted. Keep this window open.');};
    rec.onstop=async()=>{
      clearInterval(recordingTimer);clearTimeout(goQuiet);stream.getTracks().forEach(t=>t.stop());recorder=null;
      card.scene=$('scene').value.trim();card.audio=new Blob(parts,{type:rec.mimeType||parts[0]?.type||'audio/webm'});
      if(!card.audio.size){setBusy(false);setMode('Ready','No audio arrived. Try recording again.');return;}
      card.date=Date.now();
      await finishLocal(card);
    };
    rec.start();syncControls();setMode('Listening','Tell Luna your idea and what’s around you.');
    recordingTimer=setInterval(()=>{const n=Math.floor((Date.now()-started)/1000);$('timer').textContent=Math.floor(n/60)+':'+String(n%60).padStart(2,'0');},250);
    goQuiet=setTimeout(()=>{if(rec.state==='recording')rec.stop();},90000);
  }catch(e){stream?.getTracks().forEach(t=>t.stop());recorder=null;setBusy(false);
    setMode('Ready',e.name==='NotAllowedError'?'Microphone access was declined. Allow it in Chrome, or write your idea below.':e.message);}
}
$('creator').addEventListener('cancel',e=>{if(busy)e.preventDefault();});
$('loftHome').addEventListener('click',e=>{if(busy){e.preventDefault();status('Finish the recording or wait for the postcard to save.');}});
// Creator Postcards are retired from the Loft; the recorder stays here unreachable.
$('camera')?.addEventListener('click',record);$('mic').onclick=record;
const modeInfo={
 work:['Work desk','Review requests from your agents will land at the desk. This room is in the build plan.'],
 create:['Film camera','This corner of the loft is resting. Today, try the laptop, the kana notebook or Luna’s quotes.'],
 explore:['Travel passport','This corner of the loft opens later. Today, try the laptop, the kana notebook or Luna’s quotes.'],
 dance:['Dance rehearsal','This corner of the loft opens later. Today, try the laptop, the kana notebook or Luna’s quotes.'],
 kitchen:['Jupiter’s kitchen','This corner of the loft opens later. Today, try the laptop, the kana notebook or Luna’s quotes.']
};
document.querySelectorAll('[data-mode]').forEach(b=>b.onclick=()=>{
  const mode=b.dataset.mode;
  if(mode==='japanese'){location.href='japanese.html';return;}
  if(!modeInfo[mode])return;
  [$('modeTitle').textContent,$('modeCopy').textContent]=modeInfo[mode];modal('modeDialog');
});
$('usb').onclick=async()=>{
  if(!navigator.serial){$('connection').textContent='Use desktop Chrome for USB.';return;}
  let port,reader,writer,deskChanged;$('usb').disabled=true;
  try{
    port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});await port.open({baudRate:115200,bufferSize:65536});
    reader=port.readable.getReader();writer=port.writable.getWriter();
    $('connection').textContent='Waiting for Luna firmware on the CoreS3.';
    const send=s=>writer.write(new TextEncoder().encode('LUNA/1 '+s+'\n'));
    let queued=Promise.resolve(),lastDesk='';
    const ascii=s=>String(s||'-').normalize('NFKD').replace(/[‘’]/g,"'").replace(/[“”]/g,'"').replace(/[^ -~]/g,'').slice(0,42)||'-';
    let deskQueue=Promise.resolve();
    async function syncDesk(){
      const d=window.LunaDesk?.snapshot();if(!d)return;
      const serialized=JSON.stringify(d);
      if(serialized===lastDesk)return;
      const temp=Number.isFinite(d.tempC)?'NYC '+Math.round(d.tempC)+'C / '+Math.round(d.tempC*9/5+32)+'F':'NYC weather unavailable';
      await send('TEMP '+temp);
      for(let i=0;i<3;i++)await send('TASK '+i+' '+(d.done[i]?1:0)+' '+ascii(d.titles[i]));
      lastDesk=serialized;
    }
    const plain=s=>String(s).normalize('NFKD').replace(/[‘’]/g,"'").replace(/[“”]/g,'"').replace(/[–—]/g,'-').replace(/…/g,'...').replace(/[^ -~]/g,'').slice(0,140);
    let quoteQueue=Promise.resolve(),quotesSent=false;
    async function syncQuotes(){
      const lines=(window.LunaQuotes?.list()||[]).map(plain).filter(Boolean).slice(0,24);
      if(!lines.length)return;
      await send('QUOTES BEGIN');
      for(const line of lines){await send('QUOTE '+line);await new Promise(r=>setTimeout(r,40));}
      await send('QUOTES END');
    }
    const quotesChanged=()=>{if(boardReady)quoteQueue=quoteQueue.then(syncQuotes).catch(()=>{});};
    window.addEventListener('luna-quotes-change',quotesChanged);
    port.addEventListener('disconnect',()=>window.removeEventListener('luna-quotes-change',quotesChanged),{once:true});
    let boardReady=false;
    deskChanged=()=>{if(boardReady)deskQueue=deskQueue.then(syncDesk).catch(()=>{lastDesk='';});};
    window.addEventListener('luna-desk-change',deskChanged);
    window.LunaBoard={snap:()=>send('SNAP')};
    port.addEventListener('disconnect',()=>{window.LunaBoard=null;},{once:true});
    async function boardEvent(line,frame){
      if(line==='PHOTO'){window.LunaBooth?.add(frame);return;}
      if(line.startsWith('LUNA/1 SNAPPED ')){await send('GETPHOTO');return;}
      if(line==='LUNA/1 READY'){boardReady=true;deskChanged();if(!quotesSent){quotesSent=true;quotesChanged();}return;}
      const m=/^LUNA\/1 TASKDONE ([0-2]) ([01]) (.+)$/.exec(line);
      if(!m||!window.LunaDesk)return;
      const i=Number(m[1]),d=LunaDesk.snapshot();
      if(ascii(d.titles[i])===m[3] && Boolean(d.done[i])!==(m[2]==='1'))LunaDesk.toggle(i);
    }
    const wire=new Wire(send,async f=>{
      const existing=await store('readonly',s=>s.get(f.id));
      const card=existing||draft('',makeWav(f.bytes),f.kind,'Recorded on the CoreS3');
      card.id=f.id;if(!existing)await persist(card);
      queued=queued.then(async()=>{
        while(busy)await new Promise(resolve=>setTimeout(resolve,150));
        modal('creator');await deliver(card);
        if(card.storage==='local-demo')await send('INBOX '+card.id).catch(()=>{});
      }).catch(e=>status(e.message));return;
    },s=>{$('connection').textContent=s;},boardEvent);
    while(true){const {done,value}=await reader.read();if(done)break;await wire.read(value);}
    $('connection').textContent='CoreS3 disconnected.';
  }catch(e){$('connection').textContent=e.name==='NotFoundError'?'No USB device selected.':'USB: '+e.message;}
  finally{window.LunaBoard=null;if(deskChanged)window.removeEventListener('luna-desk-change',deskChanged);reader?.releaseLock();writer?.releaseLock();if(port?.readable)await port.close().catch(()=>{});$('usb').disabled=false;}
};
soundState();$('postcard').hidden=true;
window.speechSynthesis?.getVoices();
if(location.hash==='#connections'){modal('settings');$('apiKey').focus();}
$('apiKey').addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();$('connectServices').click();}});
checkServices();refresh().catch(e=>status(e.message));
