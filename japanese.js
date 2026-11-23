'use strict';
const el = id => document.getElementById(id);
const drills = [
  { id:'food', question:'Say what you like', prompt:'すし ___ 好きです。', options:['が','を','で'], correct:0, why:'すしが好きです means “I like sushi.” Use が for the thing you like.' },
  { id:'hike', question:'Describe yesterday’s hike', prompt:'きのうのハイキングは ___ 。', options:['楽しいでした','楽しいです','楽しかったです'], correct:2, why:'楽しい becomes 楽しかった in the past. Add です to keep it polite.' },
  { id:'quiet', question:'Describe a quiet café', prompt:'___ カフェでお茶を飲みました。', options:['静かの','静かな','静かで'], correct:1, why:'静か is a な-adjective. Before カフェ, use 静かな.' },
  { id:'plan', question:'Suggest a trip', prompt:'「Let’s go to Kyoto.」', options:['京都に行きませんでした。','京都に行きましょう。','京都に行きました。'], correct:1, why:'行きます becomes 行きましょう to suggest “Let’s go.”' },
];
let round=[], at=0, corrected=false, solved=false;
let review=[];
try { const saved=JSON.parse(localStorage.getItem('luna-genki-5-review')); if(Array.isArray(saved)) review=saved; } catch {}
function storeReview() { try { localStorage.setItem('luna-genki-5-review',JSON.stringify(review)); } catch {} }
function nextRound() {
  round=[...drills].sort((a,b)=>Number(review.includes(b.id))-Number(review.includes(a.id)));
  at=0;
  showCard();
}
function showCard(){
  corrected=false;solved=false;
  const card=round[at];
  el('progress').textContent='Card '+(at+1)+' of '+round.length;
  el('question').textContent=card.question;
  el('prompt').textContent=card.prompt;
  el('answers').replaceChildren();el('feedback').textContent='';el('next').hidden=true;
  card.options.forEach((text,index)=>{
    const btn=document.createElement('button');btn.type='button';btn.className='choice';btn.lang='ja';btn.textContent=text;
    btn.onclick=()=>{
      if(solved)return;
      if(index!==card.correct){
        corrected=true;
        if(!review.includes(card.id))review.push(card.id);
        storeReview();btn.disabled=true;
        el('feedback').textContent='Try another answer. '+card.why;
        return;
      }
      solved=true;btn.setAttribute('aria-current','true');
      el('answers').querySelectorAll('button').forEach(item=>{item.disabled=true;});
      if(!corrected){review=review.filter(id=>id!==card.id);storeReview();}
      el('feedback').textContent='正解！ '+card.why;
      el('next').textContent=at+1<round.length?'Next card':'Finish drill';
      el('next').hidden=false;
    };
    el('answers').append(btn);
  });
}
el('next').onclick=()=>{
  if(at===round.length){nextRound();return;}
  at++;
  if(at<round.length){showCard();return;}
  el('progress').textContent='Lesson 5';
  el('question').textContent='Drill complete';
  el('prompt').textContent='おつかれさま！';
  el('answers').replaceChildren();
  el('feedback').textContent=review.length?'The cards you missed come first next time.':'You got through all four patterns.';
  el('next').textContent='Practise again';
};
let ready=false, active=null;
const status=text=>{el('voice-state').textContent=text;};
function showVoiceControls(){
  el('start').disabled=!ready||Boolean(active);
  el('start').hidden=Boolean(active);
  el('end').hidden=!active;el('end').disabled=Boolean(active?.closing);
  el('help').hidden=!active?.started||active.closing;
  el('voice-state').classList.toggle('active-spark',Boolean(active?.started&&!active.closing));
}
function setMode(talk){
  if(!talk&&active)endSession(active);
  el('talk').hidden=!talk;el('drill').hidden=talk;
  el('talk-tab').setAttribute('aria-pressed',String(talk));
  el('drill-tab').setAttribute('aria-pressed',String(!talk));
}
el('talk-tab').onclick=()=>setMode(true);
el('drill-tab').onclick=()=>setMode(false);
async function checkReady(){
  if(location.protocol==='file:'){
    status('Open this notebook from the running Mac companion for voice. Quick drill works here.');
    return;
  }
  try{
    const response=await fetch('/api/status',{cache:'no-store'});
    if(!response.ok)throw Error();
    ready=Boolean((await response.json()).ai);
    const canTalk=Boolean(navigator.mediaDevices?.getUserMedia && window.RTCPeerConnection);
    if(!canTalk){ready=false;status('Open the Mac companion in a browser with microphone access.');}
    else if(!ready){status('Add your AI service key in the Loft to speak with Luna.');el('setup-info').hidden=false;}
    else status('Ready when you are. Luna will use the microphone during your call.');
  }catch{status('The Mac companion is unavailable. You can still use Quick drill.');}
  showVoiceControls();
}
function signal(call,type,content) {
  if(call.channel?.readyState!=='open')return false;
  const message={type,event_id:crypto.randomUUID()};
  if(content){message.content=content;message.delegation_id=null;}
  try{call.channel.send(JSON.stringify(message));return true;}catch{return false;}
}
function finish(call,message) {
  if(active!==call)return;
  active=null;
  for(const name of ['iceTimer','startupTimer','endTimer','lostTimer']) clearTimeout(call[name]);
  call.abort.abort();call.stream?.getTracks().forEach(t=>t.stop());
  try{call.channel?.close();}catch{}
  call.peer?.close();
  el('remote').pause();el('remote').srcObject=null;el('play').hidden=true;
  showVoiceControls();status(message);
}
function endSession(call) {
  if(active!==call || call.closing)return;
  call.closing=true;
  call.stream?.getTracks().forEach(t=>t.stop());
  showVoiceControls();
  if(!call.offerSent){finish(call,'Session ended.');return;}
  status('Ending conversation…');
  if(call.channel?.readyState==='open')signal(call,'session.close');
  // A provider answer may still be in flight. Complete negotiation with the mic
  // stopped so that the data channel can close an already-created session.
  call.endTimer=setTimeout(()=>finish(call,'Connection closed.'),call.started?3000:25000);
}
function waitForIce(call){
  if(call.peer.iceGatheringState==='complete')return Promise.resolve();
  return new Promise((resolve,reject)=>{
    const clear=()=>{
      clearTimeout(call.iceTimer);call.peer.removeEventListener('icegatheringstatechange',change);
      call.abort.signal.removeEventListener('abort',abort);
    };
    const abort=()=>{clear();reject(Error('Connection cancelled.'));};
    const change=()=>{if(call.peer.iceGatheringState==='complete'){clear();resolve();}};
    call.peer.addEventListener('icegatheringstatechange',change);
    call.abort.signal.addEventListener('abort',abort,{once:true});
    call.iceTimer=setTimeout(()=>{clear();reject(Error('Audio negotiation timed out. Check your network and try again.'));},10000);
    change();
  });
}
function onEvent(call,event){
  if(active!==call)return;
  let msg;try {msg=JSON.parse(event.data);}catch{return;}
  if(msg.type==='session.closed'){finish(call,'Conversation complete. You can revisit the lesson in Quick drill.');return;}
  if(call.closing){if(msg.type==='session.started')signal(call,'session.close');return;}
  if(msg.type==='session.started'&&!call.started){
    call.started=true;clearTimeout(call.startupTimer);
    call.stream.getAudioTracks().forEach(t=>{t.enabled=true;});
    showVoiceControls();status('Live with Luna · speak naturally'+(call.model?' · '+call.model:''));
    signal(call,'session.commentary.append','Greet Tanya in Japanese and ask her which foods she likes. Then listen.');
  } else if(msg.type==='session.output_transcript.delta'&&typeof msg.delta==='string'){
    const caption=el('captions');caption.textContent=(caption.textContent+msg.delta).slice(-1600);
    el('caption-box').hidden=false;caption.scrollTop=caption.scrollHeight;
  } else if(msg.type==='error'){
    signal(call,'session.close');finish(call,'The voice connection reported an error. Try starting again.');
  }
}
async function startSession(){
  if(!ready||active)return;
  const call={abort:new AbortController(),started:false,closing:false};
  active=call;showVoiceControls();
  el('captions').textContent='';el('caption-box').hidden=true;
  status('Opening microphone…');
  try {
    const stream=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true,channelCount:1}});
    if(active!==call){stream.getTracks().forEach(t=>t.stop());return;}
    call.stream=stream;
    stream.getAudioTracks().forEach(t=>{t.enabled=false;});
    const peer=new RTCPeerConnection();call.peer=peer;
    stream.getAudioTracks().forEach(t=>peer.addTrack(t,stream));
    const chan=peer.createDataChannel('oai-events');call.channel=chan;
    chan.onopen=()=>{if(call.closing)signal(call,'session.close');};
    chan.onmessage=event=>onEvent(call,event);
    chan.onclose=()=>{if(active===call)finish(call,call.closing?'Session ended.':'Voice connection lost. Start again when ready.');};
    peer.onconnectionstatechange=()=>{
      if(active!==call)return;
      clearTimeout(call.lostTimer);
      if(peer.connectionState==='failed')finish(call,'Audio connection failed. Check your network and try again.');
      else if(peer.connectionState==='disconnected')call.lostTimer=setTimeout(()=>finish(call,'Audio connection lost.'),4500);
    };
    peer.ontrack=event=>{
      if(active!==call||call.closing)return;
      el('remote').srcObject=event.streams[0]||new MediaStream([event.track]);
      el('remote').play().catch(()=>{if(active===call)el('play').hidden=false;});
    };
    status('Connecting to Luna…');
    await peer.setLocalDescription(await peer.createOffer());
    await waitForIce(call);
    if(active!==call)return;
    call.offerSent=true;
    const response=await fetch('/api/japanese/session',{method:'POST',headers:{'Content-Type':'application/sdp'},body:peer.localDescription.sdp,signal:call.abort.signal});
    if(!response.ok){const reason=await response.json().catch(()=>({}));throw Error(reason.error||'The companion could not start this session.');}
    const answer=await response.text();
    call.model=response.headers.get('X-Luna-Live-Model')||'';
    if(active!==call)return;
    call.startupTimer=setTimeout(()=>finish(call,'Luna did not connect. Check your microphone and network.'),15000);
    await peer.setRemoteDescription({type:'answer',sdp:answer});
  }catch(error){
    if(active===call)finish(call, error.name==='NotAllowedError'?'Allow microphone access in your browser to speak with Luna.':String(error.message||'Could not open the microphone.'));
  }
}
el('start').onclick=startSession;
el('end').onclick=()=>{if(active)endSession(active);};
el('repeat').onclick=()=>{if(active?.started)signal(active,'session.commentary.append','Repeat just your last Japanese phrase for the learner now.');};
el('slower').onclick=()=>{if(active?.started){signal(active,'session.instructions.append','Speak a little more slowly for the rest of this practice.');signal(active,'session.commentary.append','Repeat your most recent Japanese phrase more slowly now.');}};
el('play').onclick=async()=>{try{await el('remote').play();el('play').hidden=true;}catch{status('Check the browser’s audio playback permission.');}};
window.addEventListener('pagehide',()=>{if(active){signal(active,'session.close');finish(active,'Session ended.');}});
nextRound();checkReady();
