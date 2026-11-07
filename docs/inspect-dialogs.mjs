import fs from 'node:fs';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const evidence=JSON.parse(fs.readFileSync(fileURLToPath(new URL('./rea-resources.json',import.meta.url)),'utf8'));
const path=process.argv[2]||evidence.normalized_result.artifact.path;
const file=fs.readFileSync(path);
if(crypto.createHash('sha256').update(file).digest('hex')!==evidence.normalized_result.artifact.sha256)throw Error('Binary identity mismatch');
const dialogs=[];
for(const resource of evidence.normalized_result.resources.filter(r=>r.type.id===5)) {
  const {offset,bytes}=resource.payload.location;
  const b=file.subarray(offset,offset+bytes);
  let p=0;
  const word=()=>{const x=b.readUInt16LE(p);p+=2;return x;};
  const dword=()=>{const x=b.readUInt32LE(p);p+=4;return x;};
  const field=()=>{let x=word();if(x===0xffff)return {ordinal:word()};let s='';while(x){s+=String.fromCharCode(x);x=word();}return s;};
  const extended=b.readUInt16LE(2)===0xffff;
  let style,count;
  if(extended){word();word();dword();dword();style=dword();count=word();}
  else{style=dword();dword();count=word();}
  for(let i=0;i<4;i++)word();
  field();field();const title=field();
  if(style&0x40){word();if(extended){word();p+=2;}field();}
  const controls=[];
  for(let i=0;i<count;i++) {
    p=(p+3)&~3;
    let controlStyle;
    if(extended){dword();dword();controlStyle=dword();}
    else{controlStyle=dword();dword();}
    const rect=[];for(let j=0;j<4;j++)rect.push(word());
    const id=extended?dword():word(),cls=field(),text=field(),extra=word();
    p+=extra;
    controls.push({id,class:cls,text,style:'0x'+controlStyle.toString(16),rect});
  }
  if(p>b.length)throw Error('Dialog bounds exceeded');
  dialogs.push({resource:resource.name.id,title,source_offset:offset,controls});
}
console.log(JSON.stringify({source_sha256:evidence.normalized_result.artifact.sha256,evidence_id:evidence.evidence_id,dialogs},null,2));
