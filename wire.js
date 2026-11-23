function makeWav(pcm){
  const header=new ArrayBuffer(44),v=new DataView(header);
  const str=(i,s)=>{for(let j=0;j<s.length;j++)v.setUint8(i+j,s.charCodeAt(j));};
  str(0,'RIFF');v.setUint32(4,36+pcm.length,true);str(8,'WAVE');str(12,'fmt ');
  v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);v.setUint32(24,16000,true);
  v.setUint32(28,32000,true);v.setUint16(32,2,true);v.setUint16(34,16,true);str(36,'data');v.setUint32(40,pcm.length,true);
  return new Blob([header,pcm],{type:'audio/wav'});
}
function crc32(bytes){
  let crc=0xffffffff;
  for(const byte of bytes){crc^=byte;for(let b=0;b<8;b++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}
  return (~crc)>>>0;
}
class Wire {
  constructor(send,save,status,onEvent=()=>{}){this.onEvent=onEvent;this.send=send;this.save=save;this.status=status;this.line='';this.frame=null;this.request=null;this.requestAt=0;}
  async read(data){
    for(let i=0;i<data.length;){
      if(this.frame){
        const f=this.frame,n=Math.min(f.bytes.length-f.pos,data.length-i);
        f.bytes.set(data.subarray(i,i+n),f.pos);f.pos+=n;i+=n;
        if(f.pos===f.bytes.length){
          this.frame=null;
          if(f.photo){await this.onEvent('PHOTO',f);continue;}
          this.request=null;
          if(crc32(f.bytes)!==f.crc)throw Error('USB transfer interrupted. Reconnect to retrieve the clip again.');
          await this.save(f);await this.send('ACK '+f.id);this.status('CoreS3 connected · last postcard saved on this Mac.');
        }
        continue;
      }
      const c=data[i++];
      if(c!==10){if(c>=32&&c<127&&this.line.length<160)this.line+=String.fromCharCode(c);continue;}
      const line=this.line;this.line='';
      if(line==='LUNA/1 READY'||line.startsWith('LUNA/1 READY ')){line='LUNA/1 READY';await this.onEvent(line);this.status('Luna is connected. Tasks, temperature and quotes sync to the CoreS3.');continue;}
      if(line.startsWith('LUNA/1 TASKDONE ')){await this.onEvent(line);continue;}
      if(/^LUNA\/1 SNAPPED [0-9a-f]{8}$/.test(line)){await this.onEvent(line);continue;}
      const shot=/^LUNA\/1 PHOTO ([0-9a-f]{8}) (\d{2,3}) (\d{2,3})$/.exec(line);
      if(shot){
        const w=Number(shot[2]),h=Number(shot[3]);
        if(w===320&&h===240)this.frame={photo:true,id:shot[1],width:w,height:h,bytes:new Uint8Array(w*h*2),pos:0};
        continue;
      }
      const loaded=/^LUNA\/1 QUOTES SAVED (\d{1,2})$/.exec(line);
      if(loaded){this.status('CoreS3 connected · '+loaded[1]+' quotes loaded on the compact.');continue;}
      const offer=/^LUNA\/1 OFFER ([0-9a-f]{16})$/.exec(line);
      if(offer && (this.request!==offer[1] || Date.now()-this.requestAt>5000)){
        this.request=offer[1];this.requestAt=Date.now();await this.send('GET '+offer[1]);continue;
      }
      const info=/^LUNA\/1 AUDIO ([0-9a-f]{16}) (reel|substack) (\d{1,6}) ([0-9a-f]{8})$/.exec(line);
      if(info&&info[1]===this.request){
        const samples=Number(info[3]);if(samples<1||samples>384000)throw Error('Invalid clip length.');
        this.frame={id:info[1],kind:info[2],crc:parseInt(info[4],16),bytes:new Uint8Array(samples*2),pos:0};
      }
    }
  }
}
