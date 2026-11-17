// BreadBox: grip specs for the articulated hand (js/room3d-hands.js solves them).
// All in the held object's local space: x = thickness (front +x, x in [-T/2, T/2]), y = up (0..H),
// z = width (spine / hinge at -W/2, free edge at +W/2). A hand frame: o (origin), ex = the palm's normal
// (towards the object), ey = the thumb side, ez = towards the wrist; the model's palm surface is at x = -7.
'use strict';
(function(G){
 const nrm=v=>{const l=Math.hypot(v[0],v[1],v[2])||1;return[v[0]/l,v[1]/l,v[2]/l];};
 const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
 const rot=(v,ax,an)=>{const c=Math.cos(an),s=Math.sin(an),k=nrm(ax),d=k[0]*v[0]+k[1]*v[1]+k[2]*v[2],x=cross(k,v);return v.map((q,i)=>q*c+x[i]*s+k[i]*d*(1-c));};
 // a frame whose model point (-7, hy, hz) (on the palm) sits at c
 function place(c,ex,ey,ez,hy,hz){return{o:[0,1,2].map(i=>c[i]+7*ex[i]-hy*ey[i]-hz*ez[i]),ex,ey,ez};}
 const BOX=(RH,b)=>RH.box([-b.T/2,0,-b.W/2],[b.T/2,b.H,b.W/2]);
 function spec(RH,name,b,o={}){
  const T=b.T,H=b.H,W=b.W,mit=!!o.mitten,P=RH.POSES;
  if(name==='view'||name==='card'){
   // the edge pinch (holding a case up to look at it): the free edge sits in the crease at the knuckles, the palm
   // turned out past the edge (phi), the fingers wrap round behind and press the back, the thumb on the front
   const gy=o.gy==null?.5:o.gy,phi=o.phi==null?1.6:o.phi,psi=o.psi==null?.55:o.psi;
   let ex=[Math.cos(phi),0,-Math.sin(phi)],ey=[0,1,0],ez=[Math.sin(phi),0,Math.cos(phi)];
   ey=rot(ey,ex,psi);ez=rot(ez,ex,psi);
   const F=place([-T/2,H*gy,W/2],ex,ey,ez,o.hy==null?40:o.hy,o.hz==null?-36:o.hz);
   return{F,base:RH.frameMat(F),approach:ex,reach:40,over:30,boxes:[BOX(RH,b),...(o.extra||[])],mitten:mit,
    thumbTo:[T/2+8,H*gy+(o.ty==null?6:o.ty),W/2-(o.tz==null?24:o.tz)],
    pre:RH.pose({thumb:[.2,0,0,.1],index:[.1,.2,.1,.06],middle:[.12,.22,.12,0],ring:[.14,.24,.12,-.05],pinky:[.16,.26,.14,-.1]}),
    curl:{index:[1.4,1.6,.9],middle:[1.45,1.65,.95],ring:[1.5,1.7,1],pinky:[1.5,1.7,1]}};
  }
  if(name==='shelf'){
   // taking a box out of the shelf: the hand in front of its spine (palm towards it), index and middle fingers
   // hooked over the top edge onto its top, the thumb on the spine below; neighbours and boards are in o.extra
   const ex=nrm([0,o.tilt==null?0:o.tilt,1]);let ez=[0,-1,-.35];const d=ez[0]*ex[0]+ez[1]*ex[1]+ez[2]*ex[2];ez=nrm(ez.map((v,i)=>v-ex[i]*d));
   const ey=cross(ez,ex);
   const F=place([0,H-(o.drop==null?30:o.drop),-W/2],ex,ey,ez,o.hy==null?20:o.hy,o.hz==null?-48:o.hz);
   return{F,base:RH.frameMat(F),approach:ex,reach:50,over:20,boxes:[BOX(RH,b),...(o.extra||[])],mitten:mit,
    thumbTo:[0,H*(o.tg==null?.7:o.tg),-W/2-8],
    pre:RH.pose({thumb:[.3,0,0,.15],index:[-.1,.1,.05,.05],middle:[-.1,.1,.05,0],ring:[.1,.2,.1,-.05],pinky:[.2,.3,.2,-.1]}),
    curl:{index:[1.3,1.5,.9],middle:[1.3,1.5,.9],ring:[1.4,1.6,.9],pinky:[1.4,1.6,.9]}};
  }
  if(name==='lid'){
   // PS1 open: the lid's free edge pinched between the thumb (on the front) and the index finger (curled under
   // its lip); the other fingers fold in. o.lid = the lid's box in this space (it moves with the hand afterwards)
   const phi=o.phi==null?1.5:o.phi,psi=o.psi==null?.7:o.psi;
   let ex=[Math.cos(phi),0,-Math.sin(phi)],ey=[0,1,0],ez=[Math.sin(phi),0,Math.cos(phi)];
   ey=rot(ey,ex,psi);ez=rot(ez,ex,psi);
   const F=place([o.cx==null?-T/2:o.cx,H*(o.gy==null?.55:o.gy),W/2],ex,ey,ez,o.hy==null?40:o.hy,o.hz==null?-30:o.hz);
   return{F,base:RH.frameMat(F),approach:ex,reach:40,over:30,boxes:[BOX(RH,b),...(o.extra||[])],mitten:mit,order:['index','middle','ring','pinky','thumb'],
    thumbTo:[T/2+8,H*(o.gy==null?.55:o.gy)+6,W/2-16],
    pre:RH.pose({thumb:[.3,0,0,.1],index:[.2,.3,.2,.05],middle:[1.1,1.3,.8,0],ring:[1.2,1.4,.9,-.05],pinky:[1.3,1.4,.9,-.1]}),
    curl:{index:[1.5,1.7,1]}};
  }
  throw new Error('grip '+name);
 }
 const api={spec};
 if(typeof module!=='undefined'&&module.exports)module.exports=api;else G.RoomGrips=api;
})(typeof window!=='undefined'?window:globalThis);
