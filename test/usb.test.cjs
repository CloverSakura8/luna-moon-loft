const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const functions=fs.readFileSync(path.join(__dirname,'../wire.js'),'utf8');
const {Wire,crc32,makeWav}=vm.runInNewContext(functions+';({Wire,crc32,makeWav})',{Blob});
const id='123456789abcdef0',pcm=Buffer.from([20,10,0,13,255,97]);
function frame(crc){
 return Buffer.concat([Buffer.from('LUNA/1 OFFER '+id+'\nLUNA/1 AUDIO '+id+' reel 3 '+crc.toString(16).padStart(8,'0')+'\n'),pcm,Buffer.from('\r\n')]);
}
test('USB handles split frames, binary data and commit before acknowledgement',async()=>{
 const events=[];
 const wire=new Wire(async s=>events.push(s),async f=>{assert.deepEqual([...f.bytes],[...pcm]);events.push('saved')},()=>{});
 for(const byte of frame(crc32(pcm))) await wire.read(Uint8Array.of(byte));
 assert.deepEqual(events,['GET '+id,'saved','ACK '+id]);
});
test('CRC is standard CRC32, rejected or unsaved recordings cannot be acknowledged',async()=>{
 assert.equal(crc32(Buffer.from('123456789')),0xcbf43926);
 for(const [crc,reason] of [[0,'USB transfer interrupted'],[crc32(pcm),'storage full']]){
  const writes=[];
  const wire=new Wire(async s=>writes.push(s),async()=>{throw Error('storage full')},()=>{});
  await assert.rejects(wire.read(frame(crc)),new RegExp(reason));
  assert.deepEqual(writes,['GET '+id]);
 }
});
test('factory logs and unrelated audio frames cause no writes',async()=>{
 const writes=[];const wire=new Wire(async x=>writes.push(x),async()=>{throw Error()},()=>{});
 await wire.read(Buffer.from('Factory demo 1\nLUNA/1 AUDIO '+id+' reel 30 00000000\n'));
 assert.deepEqual(writes,[]);
});
test('Mac audio file contains signed mono 16 kHz PCM with the original samples',async()=>{
 const wave=Buffer.from(await makeWav(pcm).arrayBuffer());
 assert.equal(wave.toString('ascii',0,4),'RIFF');
 assert.equal(wave.readUInt32LE(4),wave.length-8);assert.equal(wave.readUInt32LE(24),16000);
 assert.equal(wave.readUInt16LE(22),1);assert.equal(wave.readUInt16LE(34),16);
 assert.equal(wave.readUInt32LE(40),pcm.length);assert.deepEqual(wave.subarray(44),pcm);
});
