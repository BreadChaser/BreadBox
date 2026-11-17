// BreadBox: the articulated hand. Rig (17 bones from the room file), forward kinematics,
// grip poses solved against the held object (each joint curls until that bone touches a surface), the
// penetration check (mm) and pose blending. Shared by the live mock, the bake and the node tests.
// Bones: 0 sleeve, 1 palm, 2-4 thumb (metacarpal, proximal, distal), 5-7 index, 8-10 middle, 11-13 ring,
// 14-16 pinky. A pose is a Float32Array(34): [flex, spread] per bone (radians; 0 = the model's rest pose).
'use strict';
(function(G){
 const NB=17,DIG=['thumb','index','middle','ring','pinky'],CH=d=>{const b=2+3*DIG.indexOf(d);return[b,b+1,b+2];};
 const nrm=v=>{const l=Math.hypot(v[0],v[1],v[2])||1;return[v[0]/l,v[1]/l,v[2]/l];};
 const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
 const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
 function mul(a,b){const o=new Array(16);for(let c=0;c<4;c++)for(let r=0;r<4;r++){let s=0;for(let k=0;k<4;k++)s+=a[k*4+r]*b[c*4+k];o[c*4+r]=s;}return o;}
 const T4=(x,y,z)=>[1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1];
 function rotAx(ax,a){const[x,y,z]=nrm(ax),c=Math.cos(a),s=Math.sin(a),t=1-c;
  return[t*x*x+c,t*x*y+s*z,t*x*z-s*y,0, t*x*y-s*z,t*y*y+c,t*y*z+s*x,0, t*x*z+s*y,t*y*z-s*x,t*z*z+c,0, 0,0,0,1];}
 const about=(p,R)=>mul(T4(p[0],p[1],p[2]),mul(R,T4(-p[0],-p[1],-p[2])));
 function inv(M){   // rigid or mirrored (orthonormal 3x3 + translation)
  const R=[M[0],M[4],M[8],0,M[1],M[5],M[9],0,M[2],M[6],M[10],0,0,0,0,1],t=[M[12],M[13],M[14]];
  R[12]=-(R[0]*t[0]+R[4]*t[1]+R[8]*t[2]);R[13]=-(R[1]*t[0]+R[5]*t[1]+R[9]*t[2]);R[14]=-(R[2]*t[0]+R[6]*t[1]+R[10]*t[2]);return R;}
 const frameMat=F=>[F.ex[0],F.ex[1],F.ex[2],0,F.ey[0],F.ey[1],F.ey[2],0,F.ez[0],F.ez[1],F.ez[2],0,F.o[0],F.o[1],F.o[2],1];

 // ---- the rig, from a parsed BBR1 room file (anchors hb2..hb16, parts hand / hand_sleeve / hand_mitt)
 function rig(f){
  const A=f.anchors,P={};f.parts.forEach(p=>P[p.name]=p);
  const B=[];for(let k=2;k<NB;k++){const a=A['hb'+k];B[k]={h:a.slice(0,3),f:a.slice(3,6),s:a.slice(6,9),p:Math.round(a[9])};}
  const dv=new DataView(f.vb.buffer,f.vb.byteOffset,f.vb.byteLength);
  // contact samples of a part range: its vertices, triangle centres and edge midpoints (each = 1..3 vertices)
  function samples(parts){
   const vi=new Map(),V=[],S=[],tri=[];
   const vid=i=>{if(vi.has(i))return vi.get(i);const o=i*(f.stride||16),k=V.length/4;V.push(dv.getInt16(o,true),dv.getInt16(o+2,true),dv.getInt16(o+4,true),dv.getInt16(o+6,true));vi.set(i,k);return k;};
   for(const n of parts){const p=P[n];for(let t=p.first;t<p.first+p.count;t+=3)tri.push([vid(f.ib[t]),vid(f.ib[t+1]),vid(f.ib[t+2])]);}
   const nv=V.length/4,seen=new Set(),add=(ids)=>{const k=ids.slice().sort((a,b)=>a-b).join(',');if(seen.has(k))return;seen.add(k);S.push(ids);};
   for(let i=0;i<nv;i++)add([i]);
   for(const[a,b,c]of tri){add([a,b]);add([b,c]);add([c,a]);add([a,b,c]);}
   const slot=new Int8Array(nv);for(let i=0;i<nv;i++)slot[i]=V[i*4+3];
   // a sample belongs to every bone of its vertices (the digit tests use the deepest one)
   return{V:new Float32Array(V),slot,nv,S,bone:S.map(ids=>Math.max(...ids.map(i=>slot[i])))};
  }
  const hand=P.hand,sl=P.hand_sleeve,mt=P.hand_mitt;
  return{B,ranges:{hand:[hand.first,hand.count],sleeve:[sl.first,sl.count],mitt:[mt.first,mt.count]},sleevePivot:sl.pivot,
   bare:samples(['hand']),mitt:samples(['hand_mitt']),A:{hand:A.hand,hand2:A.hand2,hand3:A.hand3}};
 }
 // ---- forward kinematics: base = the hand's matrix (palm bone); sleeve = its own matrix (or base)
 function fk(R,base,pose,sleeve){
  const M=new Array(NB);M[0]=sleeve||base;M[1]=base;
  for(let k=2;k<NB;k++){const b=R.B[k],fl=pose[k*2],sp=pose[k*2+1];
   let L=fl?rotAx(b.f,fl):null;if(sp)L=L?mul(rotAx(b.s,sp),L):rotAx(b.s,sp);
   M[k]=L?mul(M[b.p],about(b.h,L)):M[b.p];}
  return M;
 }
 // ---- boxes (in the solve space): axis-aligned lo/hi, or given in a frame (Mi = space -> box)
 const box=(lo,hi,M)=>({lo,hi,Mi:M?inv(M):null});
 let PBOX=-1;
 function penPoint(boxes,x,y,z){
  let worst=-1e9;
  for(let bi=0;bi<boxes.length;bi++){const b=boxes[bi];let p0=x,p1=y,p2=z;
   if(b.Mi){const m=b.Mi;p0=m[0]*x+m[4]*y+m[8]*z+m[12];p1=m[1]*x+m[5]*y+m[9]*z+m[13];p2=m[2]*x+m[6]*y+m[10]*z+m[14];}
   const d=Math.min(p0-b.lo[0],b.hi[0]-p0,p1-b.lo[1],b.hi[1]-p1,p2-b.lo[2],b.hi[2]-p2);   // > 0 inside: depth
   if(d>worst){worst=d;PBOX=bi;}}
  return worst;
 }
 // max penetration (mm) of a sample set posed by M; `only` = bones to test (null = all); returns {max, bone}
 function pen(Sm,M,boxes,only){
  const W=new Float32Array(Sm.nv*3),V=Sm.V;
  for(let i=0;i<Sm.nv;i++){const m=M[Sm.slot[i]],x=V[i*4],y=V[i*4+1],z=V[i*4+2];
   W[i*3]=m[0]*x+m[4]*y+m[8]*z+m[12];W[i*3+1]=m[1]*x+m[5]*y+m[9]*z+m[13];W[i*3+2]=m[2]*x+m[6]*y+m[10]*z+m[14];}
  let best=-1e9,bb=-1,bx=-1;
  for(let s=0;s<Sm.S.length;s++){const ids=Sm.S[s];if(only&&!only[Sm.bone[s]])continue;
   let x=0,y=0,z=0;for(const i of ids){x+=W[i*3];y+=W[i*3+1];z+=W[i*3+2];}const n=ids.length;
   const d=penPoint(boxes,x/n,y/n,z/n);if(d>best){best=d;bb=Sm.bone[s];bx=PBOX;}}
  return{max:Math.max(0,best),gap:best,bone:bb,box:bx};
 }

 // ---- poses
 const P0=()=>new Float32Array(NB*2);
 function pose(spec){const p=P0();for(const d of DIG){const v=spec[d];if(!v)continue;CH(d).forEach((k,j)=>{p[k*2]=v[j]||0;});if(v[3])p[CH(d)[0]*2+1]=v[3];}return p;}
 // The poses follow real hands at rest and reaching (photo/film reference of relaxed hands and
 // grasps): never straight; the "finger cascade" (each finger from index to pinky a little more curled), DIP
 // about two thirds of PIP (the tendon coupling), a slight fan, the thumb half-opposed beside the index.
 const POSES={
  // fingers [MCP, PIP, DIP, spread]; thumb [opposition, MCP, IP, abduction]
  open:pose({thumb:[.32,.1,.16,.18],index:[.12,.24,.16,.12],middle:[.16,.3,.2,.03],ring:[.22,.36,.24,-.06],pinky:[.28,.42,.28,-.14]}),
  relax:pose({thumb:[.4,.18,.24,.05],index:[.3,.45,.3,.05],middle:[.36,.52,.34,0],ring:[.44,.58,.38,-.04],pinky:[.52,.64,.42,-.08]}),
  fist:pose({thumb:[.9,.5,.6,0],index:[1.45,1.6,.9,0],middle:[1.5,1.65,.95,0],ring:[1.5,1.65,.95,0],pinky:[1.5,1.6,.9,0]}),
  // the hand picker: raised and open, a natural soft curve (not flat), fingers slightly fanned
  show:pose({thumb:[.18,.08,.14,.12],index:[.06,.16,.1,.1],middle:[.08,.2,.13,.02],ring:[.12,.25,.16,-.05],pinky:[.17,.3,.2,-.12]}),
 };
 // a hand's own small differences (the left hand is not a mirror copy): +-amt per joint, from a seed
 function vary(p,seed,amt=.06){const o=copy(p);let x=seed*9301+49297;const r=()=>((x=(x*9301+49297)%233280)/233280-.5)*2;
  for(let k=2;k<NB;k++){o[k*2]+=r()*amt;o[k*2+1]+=r()*amt*.4;}return o;}
 const LIM={thumb:[[-.3,1.9],[-.3,1.0],[-.3,1.3]],f:[[-.35,1.55],[0,1.8],[0,1.25]]};
 const RATIO={thumb:[1,.7,.85],f:[1,1.15,.75]};
 function lerpPose(a,b,k){const o=P0();for(let i=0;i<o.length;i++)o[i]=a[i]+(b[i]-a[i])*k;return o;}
 // overlapping action: the index leads, each finger after it a little later (the thumb with the index)
 const LAG={thumb:0,index:0,middle:.06,ring:.11,pinky:.16};
 const lag=(d,k)=>Math.max(0,Math.min(1,(k-LAG[d])/(1-.16)));
 // staged: a curling joint follows the one before it (proximal first, like the solver's wrap), an opening joint
 // goes first from the tip (the reverse); spreads move with the first joint
 function stagePose(a,b,k){const o=P0();for(let i=0;i<o.length;i++)o[i]=a[i]+(b[i]-a[i])*k;
  // The joints overlap more (each moves over 3/4 of the step, was 1/2): no fingertip whip
  for(const d of DIG){const ks=CH(d),kd=lag(d,k);ks.forEach((kk,j)=>{const up=b[kk*2]>a[kk*2],t0=(up?j:2-j)*.125,q=Math.max(0,Math.min(1,(kd-t0)/.75));
   o[kk*2]=a[kk*2]+(b[kk*2]-a[kk*2])*q*q*(3-2*q);});}
  return o;}
 const copy=p=>Float32Array.from(p);

 // Solve a grip. spec: {base (4x4, solve space), approach (unit vector: the hand moves along it onto the
 // object), boxes, pre (pose while approaching), curl ({digit: target [3]} = curl towards it until contact),
 // eps (mm, the contact tolerance), mitten}. Returns {base, pose, contact: {digit: bone}}.
 function solve(R,spec){
  const Sm=spec.mitten?R.mitt:R.bare,eps=spec.eps==null?.4:spec.eps,boxes=spec.boxes;
  const pp=copy(spec.pre||POSES.relax);
  if(spec.mitten)for(const d of['index','ring','pinky'])CH(d).forEach((k,j)=>{pp[k*2]=pp[CH('middle')[j]*2];});
  // 1. approach: slide the hand along `approach` until the palm or a finger touches (largest step, no penetration)
  let base=spec.base;
  if(spec.approach){const a=spec.approach;let D=spec.reach||80;const at=d=>mul(T4(a[0]*(d-D),a[1]*(d-D),a[2]*(d-D)),spec.base);
   const p=d=>pen(Sm,fk(R,at(d),pp),boxes).gap;
   while(p(0)>eps&&D<260)D+=20;          // the start must be clear: back further out until it is
   let lo=0,hi=D+(spec.over||0);if(p(lo)>eps){/* already inside at the start: keep the start */}else if(p(hi)<=eps)lo=hi;else{for(let i=0;i<22;i++){const m=(lo+hi)/2;if(p(m)<=eps)lo=m;else hi=m;}}
   base=at(lo);}
  const out=copy(pp),contact={},trail={};
  // 2. each digit curls (its three joints together, by RATIO) until a bone touches; that joint and the ones
  // before it freeze, the rest keep curling (the finger wraps the edge), until every joint touches or hits its limit
  const order=spec.order||['index','middle','ring','pinky','thumb'];
  for(const d of order){
   const tg=spec.curl&&spec.curl[d];if(!tg)continue;
   const ks=CH(d),only={};ks.forEach(k=>only[k]=1);
   if(spec.mitten&&d!=='middle'&&d!=='thumb')continue;
   let start=ks.map(k=>out[k*2]),free=[0,1,2],guard=0;const tr=trail[d]=[start.slice()];
   const lim=d==='thumb'?LIM.thumb:LIM.f;
   while(free.length&&guard++<4){
    const set=s=>{for(const j of free)out[ks[j]*2]=Math.max(lim[j][0],Math.min(lim[j][1],start[j]+(tg[j]-start[j])*s));
     if(spec.mitten&&d==='middle')for(const o of['index','ring','pinky'])CH(o).forEach((k,j)=>{out[k*2]=out[ks[j]*2];});};
    const P=s=>{set(s);return pen(Sm,fk(R,base,out),boxes,only);};
    if(P(1).gap<=eps){set(1);tr.push(ks.map(k=>out[k*2]));break;}
    const p0=P(0);
    if(p0.gap>eps){   // already pressing in at the start: open just enough
     const op=ks.map((k,j)=>lim[j][0]);let lo=0,hi=1;
     const Q=s=>{for(const j of free)out[ks[j]*2]=start[j]+(op[j]-start[j])*s;return pen(Sm,fk(R,base,out),boxes,only).gap;};
     for(let i=0;i<20;i++){const m=(lo+hi)/2;if(Q(m)<=eps)hi=m;else lo=m;}Q(hi);contact[d]=p0.bone;tr.push(ks.map(k=>out[k*2]));break;}
    let lo=0,hi=1;for(let i=0;i<22;i++){const m=(lo+hi)/2;if(P(m).gap<=eps)lo=m;else hi=m;}
    const hit=P(hi).bone;set(lo);contact[d]=hit;tr.push(ks.map(k=>out[k*2]));
    const jc=ks.indexOf(hit);start=ks.map(k=>out[k*2]);
    free=free.filter(j=>j>(jc<0?2:jc));
   }
  }
  // 3. the thumb to a target (spec.thumbTo: where its pad should rest): opposition, abduction, MCP and IP by
  // coordinate descent on distance + a penetration penalty, then pressed on (IP and MCP curl until contact)
  if(spec.thumbTo){
   const ks=CH('thumb'),tgt=spec.thumbTo,only={};ks.forEach(k=>only[k]=1);
   const vd=[];for(let i=0;i<Sm.nv;i++)if(Sm.slot[i]===ks[2])vd.push(i);
   const pad=M=>{const m=M[ks[2]];let x=0,y=0,z=0;for(const i of vd){const a=Sm.V[i*4],b=Sm.V[i*4+1],c=Sm.V[i*4+2];x+=m[0]*a+m[4]*b+m[8]*c+m[12];y+=m[1]*a+m[5]*b+m[9]*c+m[13];z+=m[2]*a+m[6]*b+m[10]*c+m[14];}return[x/vd.length,y/vd.length,z/vd.length];};
   const idx=[ks[0]*2,ks[0]*2+1,ks[1]*2,ks[2]*2],lo=[LIM.thumb[0][0],-.6,LIM.thumb[1][0],LIM.thumb[2][0]],hi=[LIM.thumb[0][1],.6,LIM.thumb[1][1],LIM.thumb[2][1]];
   const cost=()=>{const M=fk(R,base,out),q=pad(M),d=Math.hypot(q[0]-tgt[0],q[1]-tgt[1],q[2]-tgt[2]),p=pen(Sm,M,boxes,only).gap;return d+(p>eps?60+40*p:0);};
   // multi-start (the thumb can't pass through the object: a single descent gets stuck behind an edge)
   let best=null;
   for(const s0 of[[.2,0,0,0],[.8,0,.2,.2],[1.4,0,.3,.3],[1.4,.4,.3,.3],[1.8,-.3,.5,.5],[1.0,.3,.6,.8]]){
    idx.forEach((ii,j)=>{out[ii]=s0[j];});let c=cost();
    for(let st=.3;st>.004;st/=2){let moved=true,n=0;while(moved&&n++<30){moved=false;
     for(let j=0;j<4;j++)for(const sg of[1,-1]){const o=out[idx[j]],v=Math.max(lo[j],Math.min(hi[j],o+sg*st));if(v===o)continue;out[idx[j]]=v;const c2=cost();if(c2<c-1e-4){c=c2;moved=true;}else out[idx[j]]=o;}}}
    if(!best||c<best.c)best={c,v:idx.map(ii=>out[ii])};}
   idx.forEach((ii,j)=>{out[ii]=best.v[j];});contact.thumbErr=+best.c.toFixed(1);
   if(0)for(let st=.3;st>.004;st/=2){let moved=true,n=0;while(moved&&n++<30){moved=false;
    for(let j=0;j<4;j++)for(const sg of[1,-1]){const o=out[idx[j]],v=Math.max(lo[j],Math.min(hi[j],o+sg*st));if(v===o)continue;out[idx[j]]=v;const c2=cost();if(c2<c-1e-4){c=c2;moved=true;}else out[idx[j]]=o;}}}
   // press on: the IP, then the MCP, curl until the thumb touches
   for(const k of[ks[2],ks[1]]){const o=out[k*2],P=a=>{out[k*2]=a;return pen(Sm,fk(R,base,out),boxes,only);};
    if(P(o).gap>eps){P(o);continue;}let lo_=o,hi_=Math.min(o+.6,LIM.thumb[2][1]);if(P(hi_).gap<=eps){P(o);continue;}
    for(let i=0;i<20;i++){const m=(lo_+hi_)/2;if(P(m).gap<=eps)lo_=m;else hi_=m;}const hb=P(hi_).bone;P(lo_);contact.thumb=hb;break;}
   contact.thumbPad=pad(fk(R,base,out)).map(v=>+v.toFixed(1));
  }
  return{base,pose:out,contact,trail};
 }
 // the curl from the approach pose `from` (k = 0) to the grip (k = 1) along the solver's own path: each digit
 // replays its trail (the joints moved together, then froze at each contact), so every step is as clear as the
 // solve; digits without a trail (the thumb) blend staged
 function curlPose(from,g,k){const o=stagePose(from,g.pose,k);
  for(const d in g.trail){const T=g.trail[d],ks=CH(d);if(T.length<2)continue;const n=T.length-1,x=lag(d,k)*n,i=Math.min(n-1,Math.floor(x)),f=x-i;
   // The trail starts where the solver began, not always at `from`: fade that offset out over the curl
   // (it was a one-frame finger jump at the start of every curl / end of every let-go)
   const kd=lag(d,k),fo=1-kd*kd*(3-2*kd);
   ks.forEach((kk,j)=>{o[kk*2]=T[i][j]+(T[i+1][j]-T[i][j])*f+(from[kk*2]-T[0][j])*fo;o[kk*2+1]=g.pose[kk*2+1];});
   if(d==='middle'&&g.mitten)for(const q of['index','ring','pinky'])CH(q).forEach((kk,j)=>{o[kk*2]=o[ks[j]*2];});}
  return o;}
 // a little life on a held pose: the joints breathe OPEN (away from the surfaces they rest on), never in
 function breathe(p,t,amt=1,period=0){const o=copy(p);
  // a slow breath (~4 s) through the whole hand, plus a small flutter per finger; joints only ever open from the grip.
  // period: both waves are whole multiples of 2 pi / period, so any `period` window loops seamlessly
  const w1=period?2*Math.PI/period:1.55,w2=period?4*Math.PI/period:0;
  DIG.forEach((d,i)=>{const w=((.5+.5*Math.sin(t*w1+i*.45))*.7+(.5+.5*Math.sin(t*(period?w2:2.3+.21*i)+i*1.9))*.3)*amt;CH(d).forEach((k,j)=>{o[k*2]-=w*[.035,.03,.02][j]*(d==='thumb'?.7:1);});});
  return o;}

 const api={NB,DIG,CH,rig,fk,pen,box,solve,pose,POSES,lerpPose,stagePose,curlPose,vary,breathe,copy,frameMat,inv,mul,T4,rotAx,about};
 if(typeof module!=='undefined'&&module.exports)module.exports=api;else G.RoomHands=api;
})(typeof window!=='undefined'?window:globalThis);
