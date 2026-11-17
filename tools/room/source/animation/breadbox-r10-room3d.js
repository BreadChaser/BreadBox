// BreadBox: the room main menu as a real 3D scene. The room and every object are PS1-style
// low-poly models built by script in Blender (room3d/blender/*.py) and exported to a small binary
// file per room style (BBR1, room3d/SPEC.md); this mock loads those files and draws them with
// WebGL at the 3DS's native 400x240 (nearest textures, 16-bit atlas, baked vertex light).
// Navigation: focus a place, A glides the camera there, B glides back.
// the library is part of the 3D room (a wall unit, one shelf row per system; a row scrolls
// without end by recycling the bay geometry and streaming spines), the hand is a 3D model per room
// style, Settings is an oscilloscope, and every way back is a glide from where the camera really is.
'use strict';
const BreadBoxR10=(()=>{
 const R8=BreadBoxR8.api,R9=BreadBoxR9,{clamp,lerp,smooth,ease,outBack,rnd,lg,canvas,SERIF,TYPE,SANS}=R8;
 const DT=1/120,GLIDE=1.45,NEAR=30,FAR=9000;
 const STYLE_IDS=['rustic','apartment','night','nineties'];
 const PLACES=['continue','library','settings','together','share'];
 const NAME={continue:'Continue',library:'Library',settings:'Settings',together:'Play together',share:'Share BreadBox'};
 const GROUP={continue:'p_tv',library:'p_lib',settings:'p_set',together:'p_tog',share:'p_share'};
 const NAV={continue:{RIGHT:'library',DOWN:'together',LEFT:'settings'},library:{LEFT:'continue',RIGHT:'settings',DOWN:'share'},settings:{LEFT:'library',DOWN:'share',RIGHT:'continue'},
  together:{UP:'continue',RIGHT:'share',LEFT:'share'},share:{UP:'library',LEFT:'together',RIGHT:'together'}};
 // Settings = the oscilloscope's selector knob: five channels, each with its own trace
 const CHANNELS=[{id:'display',label:'DISPLAY'},{id:'controls',label:'CONTROLS'},{id:'saves',label:'SAVES'},{id:'about',label:'ABOUT'},{id:'room',label:'ROOM'},{id:'hand',label:'HAND'}];
 const TRACE_NAME=['Lissajous 3:2','square wave','heartbeat','rose','circle','a hand'];
 const ROOM_CH=4,HAND_CH=5,NCH=CHANNELS.length;
 // sound hook: every animation moment dispatches a named 'bb-sfx' event (js/breadbox-sfx.js listens);
 // fired at the moment of contact in the animation, timed by the world clock; sim = an offline capture world
 function sfx(w,name,x){window.dispatchEvent(new CustomEvent('bb-sfx',{detail:Object.assign({name,t:w.t,sim:!!w.sim},x||{})}));}
 // fire marks when a tracked value crosses them (dir 1 = going up, -1 = going down, 0 = both)
 function evTrack(w,key,v,marks){const E=w.ev||(w.ev={}),p=E[key];E[key]=v;if(p==null)return;
  for(const[th,name,dir,x]of marks){if(dir>=0&&p<th&&v>=th)sfx(w,name,x);if(dir<=0&&p>th&&v<=th)sfx(w,name,x);}}
 const SKINS={1:[1.06,.97,.86],2:[.96,.8,.65],3:[.72,.52,.38],4:[.47,.33,.24]};   // warm tones (x the warm base texture)

 // ------------------------------------------------------------------ BBR1 files
 function parse(b64){
  const bin=atob(b64),u8=new Uint8Array(bin.length);for(let i=0;i<bin.length;i++)u8[i]=bin.charCodeAt(i);
  const dv=new DataView(u8.buffer),str=(o,n)=>{let s='';for(let i=0;i<n&&u8[o+i];i++)s+=String.fromCharCode(u8[o+i]);return s;};
  if(str(0,4)!=='BBR1')throw new Error('not a BBR1 file');
  const nTex=dv.getUint16(8,true),nPart=dv.getUint16(10,true),nAnc=dv.getUint16(12,true),stride=dv.getUint16(14,true),nV=dv.getUint32(16,true),nI=dv.getUint32(20,true);
  let o=32;const tex=[],parts=[],anchors={};
  for(let i=0;i<nTex;i++,o+=24)tex.push({name:str(o,8),w:dv.getUint16(o+8,true),h:dv.getUint16(o+10,true),fmt:dv.getUint16(o+12,true),off:dv.getUint32(o+16,true),size:dv.getUint32(o+20,true)});
  for(let i=0;i<nPart;i++,o+=48)parts.push({name:str(o,12),mode:u8[o+12],tex:u8[o+13],flags:u8[o+14],grp:str(o+16,12),first:dv.getUint32(o+28,true),count:dv.getUint32(o+32,true),
   pivot:[dv.getFloat32(o+36,true),dv.getFloat32(o+40,true),dv.getFloat32(o+44,true)]});
  for(let i=0;i<nAnc;i++,o+=56){const v=[];for(let k=0;k<10;k++)v.push(dv.getFloat32(o+16+k*4,true));anchors[str(o,16)]=v;}
  // Version 2: light probes (an ambient cube each: +x -x +y -y +z -z, light / 2) after the anchors
  const ver=dv.getUint16(4,true),probes=[];
  if(ver>=2)for(let i=0,n=dv.getUint16(6,true);i<n;i++,o+=32)probes.push({p:[0,1,2].map(k=>dv.getFloat32(o+k*4,true)),c:[...Array(18)].map((_,k)=>u8[o+12+k]/255)});
  const vb=u8.slice(o,o+nV*stride),ib=new Uint16Array(u8.buffer.slice(o+nV*stride,o+nV*stride+nI*2));
  for(const t of tex)if(t.size)t.data=new Uint16Array(u8.buffer.slice(t.off,t.off+t.size));
  return{tex,parts,anchors,vb,ib,nV,nI,stride,probes,ver,bytes:u8.length};
 }

 // ------------------------------------------------------------------ GL
 const GLC=canvas(400,240);
 const gl=GLC.getContext('webgl',{antialias:false,preserveDrawingBuffer:true,alpha:true,premultipliedAlpha:false});
 const DERIV=gl&&gl.getExtension('OES_standard_derivatives');
 // One vertex format for everything (16 bytes): x y z i (shorts), u v (shorts, texel*16), r g b a (bytes).
 // Meshes: i = palette slot (matrix + tint). Sprites: i = size in mm, a = corner (bits 0-1) + animation
 // (bits 2-3: glow, dust, steam, scope). Scope sprites: i = 1000+dot, 2000+channel LED, 3000+channel name.
 const VS=`attribute vec4 aPos;attribute vec2 aUV;attribute vec4 aCol;attribute vec2 aUV2;uniform vec2 uLmSize;varying vec2 vUV2;
 uniform mat4 uVP,uM,uPal[20];uniform vec3 uTintP[20],uRight,uUp,uCamV;uniform vec2 uTexSize;uniform float uMode,uTime;
 uniform vec3 uSc0,uScH;uniform vec4 uScT,uScP;uniform vec2 uLab,uGlove;
 varying vec2 vUV;varying vec4 vCol;varying vec3 vW,vTint;
 float h1(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
 // the scope trace: point f (0 = beam, 1 = end of the afterglow) of channel m
 vec2 trace(float m,float f){
  float t=uTime;
  if(m<0.5){float u=t*1.7-f*4.6;return vec2(sin(3.0*u+t*.35),sin(2.0*u))*.9;}
  if(m<1.5){float x=fract(t*.45-f*.95)*2.0-1.0;return vec2(x*.95,.55*clamp(sin(x*9.42+.6)*7.0,-1.0,1.0));}
  if(m<2.5){float x=fract(t*.5-f*.95)*2.0-1.0;float q=fract(x*.75+.5);
   float y=exp(-pow((q-.5)*34.0,2.0))*.95-exp(-pow((q-.56)*26.0,2.0))*.35+exp(-pow((q-.75)*12.0,2.0))*.18-.12;return vec2(x*.95,y);}
  if(m<3.5){float a=t*1.1-f*8.5;float r=cos(2.5*a);float w=t*.25;return vec2(r*cos(a+w),r*sin(a+w))*.85;}
  if(m<4.5){float u=t*2.2-f*6.2832;return vec2(sin(u)*(.72+.12*sin(t*.8)),cos(u+.6*sin(t*.53))*.72);}
  // HAND: the beam draws the outline of an open hand (palm + thumb + four fingers), waving a little
  float a=fract(t*.22-f*.985)*6.2832,r=.36;
  vec4 ang=vec4(1.22,1.55,1.86,2.17),len=vec4(.5,.56,.5,.4);
  for(int i=0;i<4;i++){float d=mod(a-ang[i]+3.14159,6.2832)-3.14159;r+=len[i]*exp(-d*d/.012);}
  float d0=mod(a-.38+3.14159,6.2832)-3.14159;r+=.34*exp(-d0*d0/.02);
  float wv=.12*sin(t*2.0);vec2 q=vec2(cos(a+wv),sin(a+wv))*r;return vec2(q.x*.82,q.y*.8-.3);
 }
 void main(){
  vec3 p=aPos.xyz;vec4 col=aCol;vec4 w;vTint=vec3(1.0);
  if(uMode>0.5){
   float k=floor(aCol.a*255.0+0.5),c=mod(k,4.0),an=floor(k/4.0);
   vec2 cr=vec2((c==1.0||c==2.0)?1.0:-1.0,(c>=2.0)?-1.0:1.0);
   vec2 o=cr*aPos.w*0.5;
   float ph=h1(floor(p.xz));
   if(an==1.0){p.y=150.0+mod(p.y-150.0+uTime*(14.0+ph*12.0),1300.0);p.x+=sin(uTime*.4+ph*6.28)*40.0;p.z+=cos(uTime*.3+ph*5.0)*30.0;col.rgb*=.55+.45*sin(uTime*1.7+ph*20.0);}
   if(an==4.0){p.x+=sin(uTime*.21+ph*6.28)*30.0;p.y+=sin(uTime*.13+ph*4.0)*40.0;p.z+=cos(uTime*.17+ph*5.0)*30.0;col.rgb*=.3+.7*pow(.5+.5*sin(uTime*.9+ph*20.0),2.0);}   // round 5: motes in the window beam
   if(an==2.0){float u=fract(uTime*.32+ph+p.y/150.0);p.y=p.y-mod(p.y-470.0,50.0)+u*170.0;p.x+=sin(u*5.0+ph*6.0)*14.0;col.rgb*=sin(u*3.14159);o*=.6+u;}
   if(an==3.0){
    float code=aPos.w;
    if(code<1999.0){float f=(code-1000.0)/128.0;vec2 q=mix(trace(uScT.x,f),trace(uScT.y,f),uScT.z);
     if(uScP.x>0.5)q*=min(1.0,.97/max(length(q),.001));
     p=uSc0+vec3(q.x*uScH.x,q.y*uScH.y,0.0);o=cr*uScH.z*.5;
     float tail=pow(1.0-f,uScP.y*3.0+.6);col.rgb*=(.35+1.9*tail)*(1.0-.55*sin(3.14159*uScT.z));}
    else if(code<2999.0){float ch=code-2000.0;o=cr*7.0;col.rgb*=abs(ch-uScT.w)<.5?1.0:.06;}
    else{float ch=code-3000.0;o=cr*uLab*.5;col.rgb*=abs(ch-uScT.w)<.5?(.75+.25*smoothstep(.0,1.0,uScT.z)):0.0;}
   }
   float sz=max(abs(o.x),abs(o.y));   // fade a glow as the camera gets close to it; size 0 = a world-fixed vertex (round 5: god rays) fades inside ~1.4 m
   col.rgb*=sz>0.0?clamp(distance(p,uCamV)/(sz*5.2+1.0)-0.45,0.0,1.0):clamp((distance(p,uCamV)-400.0)/1000.0,0.0,1.0);
   p+=uRight*o.x+uUp*o.y;col.a=1.0;w=uM*vec4(p,1.0);
  }else{int i=int(aPos.w+0.5);w=uM*(uPal[i]*vec4(p,1.0));vTint=uTintP[i];}
  vUV2=aUV2/16.0/uLmSize;vW=w.xyz;gl_Position=uVP*w;vUV=(aUV/16.0+(uMode<0.5&&aCol.a<.999?uGlove:vec2(0.0)))/uTexSize;vCol=col;
 }`;
 const FS=`${DERIV?'#extension GL_OES_standard_derivatives : enable\n':''}precision mediump float;
 uniform sampler2D uTex,uLm;uniform float uKind,uFade,uLmOn;uniform vec3 uTint,uCam,uKey,uSkin;
 varying vec2 vUV,vUV2;varying vec4 vCol;varying vec3 vW,vTint;
 void main(){
  vec4 t=texture2D(uTex,vUV);vec3 lm=uLmOn>0.5?texture2D(uLm,vUV2).rgb*2.0:vec3(1.0);   // round 4: x lightmap (2nd stage, x2)
  if(uKind<0.5){if(t.a<0.5)discard;vec3 sk=vCol.a<.999?uSkin:vec3(1.0);
   gl_FragColor=uLmOn>2.5?vec4(fract(vUV2*8.0),0.0,1.0):uLmOn>1.5?vec4(lm*.5,1.0):vec4(t.rgb*vCol.rgb*2.0*uTint*vTint*sk*lm,1.0);}
  else if(uKind<1.5){gl_FragColor=vec4(0.0,0.0,0.0,t.a*vCol.a*uFade);}
  else if(uKind<2.5){gl_FragColor=vec4(vCol.rgb*t.a*uFade,1.0);}
  else if(uKind>4.5){vec4 c=vec4(t.rgb*vCol.rgb*2.0*uTint*lm,uFade);gl_FragColor=c;}   // real boxes cross-fading in
  else if(uKind>3.5){gl_FragColor=vec4(t.rgb*vCol.rgb*2.0*uTint*mix(vec3(1.0),uSkin,t.a),1.0);}   // the hand: texture alpha = skin mask
  else{if(t.a<0.5)discard;
   ${DERIV?'vec3 n=normalize(cross(dFdx(vW),dFdy(vW)));if(dot(n,uCam-vW)<0.0)n=-n;float l=.55+.55*max(0.0,dot(n,uKey));':'float l=1.0;'}
   gl_FragColor=vec4(t.rgb*vCol.rgb*2.0*l*uTint,1.0);}
 }`;
 // post pass: 15-bit colour (RGB555, what the PS1 wrote) with a light 4x4 ordered dither
 const PVS=`attribute vec2 aP;varying vec2 v;void main(){v=aP*.5+.5;gl_Position=vec4(aP,0.,1.);}`;
 // a slight vignette and a darkening film grain (3DS: one full-screen quad per eye, multiply blend: a 5x3
 // vertex-colour grid for the vignette x a 64x64 I4 noise texture moved every frame; SPEC.md)
 const PFS=`precision mediump float;uniform sampler2D uTex;uniform float uDither,uOn,uVig,uGrain,uT;uniform vec2 uSize;varying vec2 v;
 float hh(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
 float b2(vec2 p){return mod(2.0*p.x+3.0*p.y,4.0);}
 float bayer(vec2 p){p=floor(p);return(4.0*b2(mod(p,2.0))+b2(mod(floor(p*.5),2.0)))/16.0;}
 void main(){vec3 c=texture2D(uTex,v).rgb;vec2 q=(v-.5)*vec2(2.0,1.2);c*=1.0-uVig*smoothstep(.25,1.35,dot(q,q));
  c*=1.0-uGrain*hh(floor(gl_FragCoord.xy)+floor(uT*24.0)*vec2(37.0,17.0));if(uOn>.5){float d=(bayer(gl_FragCoord.xy)-.47)*uDither/31.0;c=floor(clamp(c+d,0.,1.)*31.0+.5)/31.0;}gl_FragColor=vec4(c,1.);}`;
 function prog(vs,fs){const p=gl.createProgram();for(const[t,s]of[[gl.VERTEX_SHADER,vs],[gl.FRAGMENT_SHADER,fs]]){const sh=gl.createShader(t);gl.shaderSource(sh,s);gl.compileShader(sh);
  if(!gl.getShaderParameter(sh,gl.COMPILE_STATUS))throw new Error('shader: '+gl.getShaderInfoLog(sh));gl.attachShader(p,sh);}
  gl.linkProgram(p);if(!gl.getProgramParameter(p,gl.LINK_STATUS))throw new Error('link: '+gl.getProgramInfoLog(p));
  const u={};for(let i=0,n=gl.getProgramParameter(p,gl.ACTIVE_UNIFORMS);i<n;i++){const a=gl.getActiveUniform(p,i);u[a.name]=gl.getUniformLocation(p,a.name);}
  const a={};for(let i=0,n=gl.getProgramParameter(p,gl.ACTIVE_ATTRIBUTES);i<n;i++){const x=gl.getActiveAttrib(p,i);a[x.name]=gl.getAttribLocation(p,x.name);}
  return{p,u,a};}
 let P0=null,PP=null,FBO=null,FTEX=null,QUAD=null;
 function initGL(){
  if(P0)return;P0=prog(VS,FS);PP=prog(PVS,PFS);
  FTEX=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,FTEX);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,400,240,0,gl.RGBA,gl.UNSIGNED_BYTE,null);
  for(const k of[gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,k,gl.NEAREST);
  for(const k of[gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,k,gl.CLAMP_TO_EDGE);
  const rb=gl.createRenderbuffer();gl.bindRenderbuffer(gl.RENDERBUFFER,rb);gl.renderbufferStorage(gl.RENDERBUFFER,gl.DEPTH_COMPONENT16,400,240);
  FBO=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,FBO);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,FTEX,0);gl.framebufferRenderbuffer(gl.FRAMEBUFFER,gl.DEPTH_ATTACHMENT,gl.RENDERBUFFER,rb);
  gl.bindFramebuffer(gl.FRAMEBUFFER,null);
  QUAD=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,QUAD);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);
 }
 function texFrom(w,h,data,fmt){
  const t=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t);gl.pixelStorei(gl.UNPACK_ALIGNMENT,2);
  if(fmt===1)gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,w,h,0,gl.RGBA,gl.UNSIGNED_SHORT_5_5_5_1,data);
  else if(fmt===2)gl.texImage2D(gl.TEXTURE_2D,0,gl.RGB,w,h,0,gl.RGB,gl.UNSIGNED_SHORT_5_6_5,data);
  else gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,w,h,0,gl.RGBA,gl.UNSIGNED_BYTE,null);
  for(const k of[gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,k,gl.NEAREST);
  for(const k of[gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,k,gl.CLAMP_TO_EDGE);
  return{t,w,h};
 }
 function canvasTex(T,cv,linear=false){gl.bindTexture(gl.TEXTURE_2D,T.t);gl.pixelStorei(gl.UNPACK_ALIGNMENT,4);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,cv);
  for(const k of[gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,k,linear?gl.LINEAR:gl.NEAREST);T.w=cv.width;T.h=cv.height;}
 function meshFrom(f,dyn=false){
  const vb=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,vb);gl.bufferData(gl.ARRAY_BUFFER,f.vb,dyn?gl.DYNAMIC_DRAW:gl.STATIC_DRAW);
  const ib=gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,ib);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,f.ib,dyn?gl.DYNAMIC_DRAW:gl.STATIC_DRAW);
  return{vb,ib,stride:f.stride||16};
 }
 // a room style, loaded on first use: parse + one vertex buffer + one index buffer + the atlas + the hand
 const ROOMS={};
 function room(id){
  if(ROOMS[id])return ROOMS[id];initGL();
  const t0=performance.now(),f=parse(window.ROOM3D[id]),m=meshFrom(f);
  const T=f.tex.map(t=>t.data?texFrom(t.w,t.h,t.data,t.fmt):texFrom(t.w,t.h,null,0));
  const parts={};f.parts.forEach(p=>parts[p.name]=p);
  const c=f.anchors.case,CASE={x0:c[0],bay:c[1],nbay:c[2],gap:c[3],rows:c[4],top:c[5],depth:c[6],upr:c[7],board:c[8]};
  return ROOMS[id]={id,f,m,T,parts,A:f.anchors,CASE,loadMs:performance.now()-t0};
 }

 // ------------------------------------------------------------------ camera (same model as R9/R8)
 function aim(pos,look){const d=[look[0]-pos[0],look[1]-pos[1],look[2]-pos[2]];return[Math.atan2(d[0],d[2]),Math.atan2(d[1],Math.hypot(d[0],d[2]))];}
 // world -> camera: x' = right.d - eye, y' = up.d, z' = fwd.d ; screen x = 200 + F x'/z' + eye F/conv
 function camera(pos,yaw,pitch,F,eye,conv,cy=120){
  const cyw=Math.cos(yaw),syw=Math.sin(yaw),cp=Math.cos(pitch),sp=Math.sin(pitch);
  const R=[cyw,0,-syw],U=[-sp*syw,cp,-sp*cyw],Fw=[cp*syw,sp,cp*cyw];
  const V=[R[0],U[0],Fw[0],0, R[1],U[1],Fw[1],0, R[2],U[2],Fw[2],0, 0,0,0,1];
  const tx=-(R[0]*pos[0]+R[1]*pos[1]+R[2]*pos[2])-eye,ty=-(U[0]*pos[0]+U[1]*pos[1]+U[2]*pos[2]),tz=-(Fw[0]*pos[0]+Fw[1]*pos[1]+Fw[2]*pos[2]);
  V[12]=tx;V[13]=ty;V[14]=tz;
  const A=(FAR+NEAR)/(FAR-NEAR),B=-2*FAR*NEAR/(FAR-NEAR);
  const Pm=[F/200,0,0,0, 0,F/120,0,0, eye*F/conv/200,(120-cy)/120,A,1, 0,0,B,0];
  const VP=mul(Pm,V),eyeW=[pos[0]+R[0]*eye,pos[1],pos[2]+R[2]*eye];
  const proj=(x,y,z)=>{const d=[x-pos[0],y-pos[1],z-pos[2]],xc=R[0]*d[0]+R[2]*d[2]-eye,yc=U[0]*d[0]+U[1]*d[1]+U[2]*d[2],zc=Fw[0]*d[0]+Fw[1]*d[1]+Fw[2]*d[2],zz=Math.max(zc,NEAR);
   return[200+F*xc/zz+eye*F/conv,cy-F*yc/zz,zz];};
  return{VP,R,U,Fw,pos,eyeW,F,proj};
 }
 function mul(a,b){const o=new Array(16);for(let c=0;c<4;c++)for(let r=0;r<4;r++){let s=0;for(let k=0;k<4;k++)s+=a[k*4+r]*b[c*4+k];o[c*4+r]=s;}return o;}
 const I4=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],HIDE=[0,0,0,0,0,0,0,0,0,0,0,0,0,-99999,0,1];
 const T4=(x,y,z)=>[1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1];
 function M3(c0,c1,c2){return[c0[0],c0[1],c0[2],0,c1[0],c1[1],c1[2],0,c2[0],c2[1],c2[2],0,0,0,0,1];}
 const rotZ=a=>{const c=Math.cos(a),s=Math.sin(a);return[c,s,0,0,-s,c,0,0,0,0,1,0,0,0,0,1];};
 const rotY=a=>{const c=Math.cos(a),s=Math.sin(a);return[c,0,-s,0,0,1,0,0,s,0,c,0,0,0,0,1];};
 const rotX=a=>{const c=Math.cos(a),s=Math.sin(a);return[1,0,0,0,0,c,s,0,0,-s,c,0,0,0,0,1];};
 const about=(p,R)=>mul(T4(...p),mul(R,T4(-p[0],-p[1],-p[2])));
 const tr=m=>[m[0],m[4],m[8],0,m[1],m[5],m[9],0,m[2],m[6],m[10],0,0,0,0,1];
 const xf=(M,p)=>[0,1,2].map(i=>M[i]*p[0]+M[4+i]*p[1]+M[8+i]*p[2]+M[12+i]);
 const nrm=v=>{const l=Math.hypot(...v)||1;return v.map(x=>x/l);};
 const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];

 // ------------------------------------------------------------------ the library: 8 rows x 3 sections = 24 slots
 // Every bay of a row is one system's section. Only owned systems are shown, packed from the top left in a
 // fixed order (gaps close up); unused slots stay empty shelf. Far away a section is ONE quad of a pre-baked
 // generic spine image (per spine style, baked once); the real spines (actual games, readable titles) exist only
 // for the section the camera glides into, and cross-fade in as it arrives.
 const SYS={
  fav:{name:'Favorites',plate:'FAVORITES',short:'Favorites',style:'mix'},
  nes:{name:'NES',plate:'NES',short:'NES',n:54,box:{T:26,H:134,W:118},prop:'gba',style:'card',band:'#2b2b30',mock:true},
  snes:{name:'Super Nintendo',plate:'SUPER NINTENDO',short:'SNES',n:88,box:{T:30,H:128,W:134},prop:'gba',style:'card',band:'#5a4a8a',mock:true},
  n64:{name:'Nintendo 64',plate:'NINTENDO 64',short:'N64',n:31,box:{T:32,H:132,W:138},prop:'gba',style:'card',band:'#b02a24',mock:true},
  gb:{name:'Game Boy',plate:'GAME BOY',short:'Game Boy',n:60,box:{T:22,H:124,W:124},prop:'gba',style:'card',band:'#6a6a64',mock:true},
  gbc:{name:'Game Boy Color',plate:'GAME BOY COLOR',short:'GB Color',n:42,box:{T:22,H:124,W:124},prop:'gba',style:'card',band:'#2a6ab0',mock:true},
  gba:{name:'Game Boy Advance',plate:'GAME BOY ADVANCE',short:'GBA',n:315,box:{T:23,H:125,W:125},prop:'gba',style:'card',band:'#3a2d7c'},
  nds:{name:'Nintendo DS',plate:'NINTENDO DS',short:'DS',n:70,box:{T:14,H:125,W:112},prop:'ps1',style:'case',band:'#d8d8dc',mock:true},
  vb:{name:'Virtual Boy',plate:'VIRTUAL BOY',short:'V. Boy',n:14,box:{T:30,H:140,W:122},prop:'gba',style:'card',band:'#a01818',mock:true},
  sms:{name:'Master System',plate:'MASTER SYSTEM',short:'Master Sys.',n:40,box:{T:20,H:140,W:118},prop:'ps1',style:'clam',band:'#d8d8d8',mock:true},
  md:{name:'Genesis',plate:'GENESIS',short:'Genesis',n:47,box:{T:22,H:140,W:118},prop:'ps1',style:'clam',band:'#c41a1a',mock:true},
  gg:{name:'Game Gear',plate:'GAME GEAR',short:'Game Gear',n:33,box:{T:18,H:130,W:110},prop:'ps1',style:'clam',band:'#1a1a1a',mock:true},
  s32x:{name:'32X',plate:'32X',short:'32X',n:12,box:{T:22,H:140,W:118},prop:'ps1',style:'clam',band:'#e0c020',mock:true},
  sat:{name:'Saturn',plate:'SATURN',short:'Saturn',n:38,box:{T:16,H:125,W:142},prop:'ps1',style:'jewel',band:'#2a3a6a',mock:true},
  dc:{name:'Dreamcast',plate:'DREAMCAST',short:'Dreamcast',n:29,box:{T:14,H:125,W:142},prop:'ps1',style:'jewel',band:'#e86a10',mock:true},
  ps1:{name:'PlayStation',plate:'PLAYSTATION',short:'PS1',n:191,box:{T:14,H:125,W:142},prop:'ps1',style:'jewel'},
  psp:{name:'PSP',plate:'PSP',short:'PSP',n:36,box:{T:14,H:140,W:100},prop:'ps1',style:'case',band:'#202024',mock:true},
  pce:{name:'TurboGrafx-16',plate:'TURBOGRAFX-16',short:'TG-16',n:24,box:{T:14,H:125,W:142},prop:'ps1',style:'jewel',band:'#e06a1a',mock:true},
  ngp:{name:'Neo Geo Pocket',plate:'NEO GEO POCKET',short:'NG Pocket',n:20,box:{T:18,H:120,W:100},prop:'gba',style:'card',band:'#1a5a9a',mock:true},
  ws:{name:'WonderSwan',plate:'WONDERSWAN',short:'WonderSwan',n:22,box:{T:18,H:120,W:100},prop:'gba',style:'card',band:'#4a8a4a',mock:true},
  lynx:{name:'Lynx',plate:'LYNX',short:'Lynx',n:26,box:{T:24,H:130,W:110},prop:'gba',style:'card',band:'#d07a10',mock:true},
  a2600:{name:'Atari 2600',plate:'ATARI 2600',short:'2600',n:66,box:{T:30,H:140,W:110},prop:'gba',style:'card',band:'#8a2a14',mock:true},
  a7800:{name:'Atari 7800',plate:'ATARI 7800',short:'7800',n:28,box:{T:30,H:140,W:110},prop:'gba',style:'card',band:'#6a6a6a',mock:true},
  jag:{name:'Jaguar',plate:'JAGUAR',short:'Jaguar',n:18,box:{T:30,H:140,W:122},prop:'gba',style:'card',band:'#a8141a',mock:true}};
 // The packing order: Favorites first, then the systems by first release date, NEWEST first
 const RELEASED={psp:'2004-12-12',nds:'2004-11-21',gba:'2001-03-21',ws:'1999-03-04',dc:'1998-11-27',ngp:'1998-10-28',gbc:'1998-10-21',
  n64:'1996-06-23',vb:'1995-07-21',ps1:'1994-12-03',sat:'1994-11-22',s32x:'1994-11-21',jag:'1993-11-23',snes:'1990-11-21',gg:'1990-10-06',
  lynx:'1989-09-01',gb:'1989-04-21',md:'1988-10-29',pce:'1987-10-30',a7800:'1986-05-01',sms:'1985-10-20',nes:'1983-07-15',a2600:'1977-09-11'};
 const ORDER=['fav',...Object.keys(RELEASED).sort((a,b)=>RELEASED[b].localeCompare(RELEASED[a]))];
 const inOrder=list=>ORDER.filter(s=>list.includes(s));   // the mock libraries: the owner's 'five' (+ Favorites), PS1+GBA, all 24
 const SETS={five:inOrder(['fav','nes','snes','gba','md','ps1']),two:inOrder(['fav','gba','ps1']),all:ORDER};
 let SYSSET='five';
 const WA=['Aero','Ancient','Arcane','Astro','Atomic','Battle','Blade','Blaze','Brave','Bubble','Chrono','Cosmic','Crystal','Cyber','Dark','Dragon','Dream','Echo','Ember','Final',
  'Frost','Galaxy','Ghost','Grand','Hyper','Iron','Jet','Jungle','Knight','Legend','Lunar','Magic','Mega','Metal','Micro','Neo','Night','Ninja','Omega','Pixel','Planet','Power',
  'Quantum','Rally','Rocket','Royal','Shadow','Sky','Solar','Space','Star','Steel','Storm','Super','Thunder','Tiny','Turbo','Ultra','Urban','Vector','Wild','Wonder','Xeno','Zero'];
 const WB=['Adventure','Arena','Bandits','Blast','Brawl','Chase','Circuit','Clash','Command','Crusade','Drift','Empire','Force','Frontier','Fury','Gear','Heroes','Hunter','Island','Kart',
  'Kingdom','League','Legacy','Machine','Mania','Mission','Odyssey','Patrol','Pinball','Quest','Racer','Raiders','Rescue','Riders','Rumble','Saga','Soccer','Squad','Strike','Tactics',
  'Tennis','Trek','Warriors','World','Zone'];
 const WC=['','','','',' 2',' 3',' DX',' Deluxe',': Remix',' 64',' Advance',' Tournament'];
 function genTitles(seed,n,keep){
  const out=new Set(keep);let i=0;
  while(out.size<n){const h=R8.ih(seed,i++,7),a=WA[Math.floor(h*WA.length)],b=WB[Math.floor(R8.ih(seed,i,9)*WB.length)],c=WC[Math.floor(R8.ih(seed,i,11)*WC.length)];out.add(a+' '+b+c);}
  return[...out].sort((x,y)=>x.localeCompare(y));
 }
 // Case dimensions: PS1 jewel 10.5 (NA / JP), 17 (PAL), 25 (2+ discs); GBA 125 x 125 x 23 (NA / EU boxes)
 const MULTI_DISC=new Set(['Final Fantasy VII','Final Fantasy VIII','Final Fantasy IX','Metal Gear Solid','Chrono Cross','Xenogears','Resident Evil 2',
  'Parasite Eve','Parasite Eve II','The Legend of Dragoon','Lunar: Silver Star Story Complete','Lunar 2: Eternal Blue Complete','Valkyrie Profile',
  'Resident Evil 3: Nemesis','Fear Effect','Fear Effect 2: Retro Helix','Star Ocean: The Second Story','Wild Arms 2','Grandia','Thousand Arms','Dino Crisis 2']);
 const discsOf=g=>g.discs||(MULTI_DISC.has(g.title)?2:1);
 const BOXOF=g=>g.sys==='ps1'?{T:OPT.thick||(discsOf(g)>1?25:region(g)==='eu'?17:10.5),H:125,W:142}:g.sys==='gba'?{T:23,H:125,W:125}:SYS[g.sys].box;
 const LIBROWS=[];        // the owned SECTIONS (kept under the old name); index = section id
 function library(rm){
  if(LIBROWS.length)return LIBROWS;
  const C=rm.CASE;
  SETS[SYSSET].forEach((sid,i)=>{
   const r=C.rows-1-Math.floor(i/C.nbay),c=i%C.nbay,xl=C.x0+c*C.bay;
   const sec={i,sid,r,c,base:r*C.gap,xl,xr:xl+C.bay,xin0:xl+C.upr/2,xin1:xl+C.bay-C.upr/2,games:[],sel:0};
   sec.cx=(sec.xl+sec.xr)/2;sec.inner=sec.xin1-sec.xin0;
   if(sid==='fav')sec.games=R8.LIB.fav.map(g=>({sys:g.sys,title:g.title}));
   else if(sid==='ps1'||sid==='gba')sec.games=genTitles(i*31+5,SYS[sid].n,R8.LIB[sid].map(g=>g.title)).map(t=>({sys:sid,title:t}));
   else sec.games=genTitles(i*31+5,SYS[sid].n,[]).map(t=>({sys:sid,title:t}));
   let x=0;
   sec.games.forEach((g,j)=>{const b=BOXOF(g),gap=1.2+rnd(i*500+j)*1.6;g.lx=x+gap+b.T/2;g.b=b;g.i=j;g.letter=(g.title.match(/[A-Z]/i)||['#'])[0].toUpperCase();x+=gap+b.T;});
   sec.total=x+2;sec.fill=Math.min(1,sec.total/sec.inner);
   LIBROWS.push(sec);
  });
  const ps1=LIBROWS.find(r=>r.sid==='ps1');if(ps1)ps1.sel=Math.max(0,ps1.games.findIndex(g=>g.title==='Crash Bandicoot'));
  return LIBROWS;
 }
 // the shelf window of a section: content offset for the camera's logical position (boxes slide behind the uprights)
 const secOff=(sec,lc)=>clamp(lc-sec.inner/2,0,Math.max(0,sec.total-sec.inner));
 const boxX=(sec,g,off)=>sec.xin0+g.lx-off;

 // ---- real-style spines (32x256 cells): a scan when we have one, else generated in the system's real style.
 // No platform logos: a neutral mark where the logo would be. Region from the serial (or a hash for mock titles).
 const SPINE={};
 const PUBS=['BREADSOFT','PIXEL HOUSE','NORTHWIND','KITE GAMES','RED OAK','LUMEN','ORBIT WORKS','TIDE','HOLLOW HILL','SUNDIAL'];
 function region(g){const h=R8.ih(g.title.length,g.title.charCodeAt(0),g.title.charCodeAt(g.title.length-1));return h<.7?'us':h<.9?'eu':'jp';}
 // a cover's dominant saturated colour (12 hue bins weighted by saturation x value; the top bin's mean,
 // lightness kept in 18-82 %), the GBA spine's background when there is no scan
 function coverColor(fr){if(!fr)return null;const W=24,H=24,c=canvas(W,H),k=c.getContext('2d');k.drawImage(fr,0,0,W,H);const d=k.getImageData(0,0,W,H).data,bin=[];
  for(let i=0;i<d.length;i+=4){const r=d[i],g=d[i+1],b=d[i+2],mx=Math.max(r,g,b),mn=Math.min(r,g,b),v=mx/255,sat=mx?(mx-mn)/mx:0;if(sat<.2||v<.15)continue;
   let hu=mx===mn?0:mx===r?((g-b)/(mx-mn))%6:mx===g?(b-r)/(mx-mn)+2:(r-g)/(mx-mn)+4;hu=(hu+6)%6;const j=Math.floor(hu*2)%12,w=sat*v;(bin[j]=bin[j]||[0,0,0,0]);bin[j][0]+=r*w;bin[j][1]+=g*w;bin[j][2]+=b*w;bin[j][3]+=w;}
  let best=null;for(const q of bin)if(q&&(!best||q[3]>best[3]))best=q;if(!best)return null;let c3=[best[0]/best[3],best[1]/best[3],best[2]/best[3]];
  const l=(Math.max(...c3)+Math.min(...c3))/510;if(l<.18)c3=c3.map(v=>v+(.18-l)*255);if(l>.82)c3=c3.map(v=>v*.82/l);return c3.map(v=>Math.max(0,Math.min(255,Math.round(v))));}
 function spineCanvas(g){
  const key=g.sys+'/'+g.title,ck=key+'/'+OPT.spines;if(SPINE[ck])return SPINE[ck];
  const cv=canvas(32,256),k=cv.getContext('2d'),sm=(window.BOXFLIP_SAMPLES||{})[key],scan=sm&&sm.spine&&App.images[sm.spine];
  if(scan&&scan.naturalWidth){k.imageSmoothingEnabled=true;k.imageSmoothingQuality='high';k.drawImage(scan,0,0,32,256);return SPINE[ck]=cv;}
  const S=SYS[g.sys],style=S.style,h=R8.ih(key.length,g.title.charCodeAt(0),g.title.charCodeAt(1)||3),rg=region(g),pub=PUBS[Math.floor(h*PUBS.length)];
  let art=[[150,40,40],[40,90,160],[220,180,40],[40,130,70],[110,50,140],[230,110,40],[30,30,34],[200,200,196]][Math.floor(h*8)];
  if(g.sys==='ps1'||g.sys==='gba'){const pal=R8.game(g.sys,g.title).pal,c=pal[1].match(/[0-9a-f]{2}/gi);if(c)art=c.map(v=>parseInt(v,16));}
  const title=(x0,y0,len,col,shadow,max=18)=>{k.save();k.translate(16,y0);k.rotate(Math.PI/2);k.textBaseline='middle';const z=R8.fitFont(k,g.title,len,max,SANS);
   k.font=`700 ${z}px ${SANS}`;if(shadow){k.fillStyle=shadow;k.fillText(g.title,1,1);}k.fillStyle=col;k.fillText(g.title,0,0);k.restore();};
  const vtext=(t,y0,col,px)=>{k.save();k.translate(16,y0);k.rotate(Math.PI/2);k.textBaseline='middle';k.font=`700 ${px}px ${SANS}`;k.fillStyle=col;k.fillText(t,0,0);k.restore();};
  const mark=(y,col)=>{k.strokeStyle=col;k.lineWidth=1.5;R8.rr(k,9,y,14,14,3);k.stroke();k.fillStyle=col;k.beginPath();k.moveTo(16,y+3);k.lineTo(20,y+7);k.lineTo(16,y+11);k.lineTo(12,y+7);k.fill();};
  // the cover's art strip (PS1/GBA: cover art; mock systems: their colours), so no two spines look alike
  const fr=g.sys==='ps1'||g.sys==='gba'?R8.boxTex(R8.game(g.sys,g.title)).front:null;
  const artStrip=(y0,y1,a)=>{k.globalAlpha=a;if(fr){k.drawImage(fr,fr.width*.03,0,fr.width*.2,fr.height,0,y0,32,y1-y0);
    k.globalCompositeOperation='soft-light';k.fillStyle=`rgb(${art})`;k.fillRect(0,y0,32,y1-y0);k.globalCompositeOperation='screen';k.fillStyle=`rgba(${art},.28)`;k.fillRect(0,y0,32,y1-y0);k.globalCompositeOperation='source-over';}
   else{const gr=k.createLinearGradient(0,y0,0,y1);gr.addColorStop(0,`rgb(${art.map(v=>Math.min(255,v*1.25|0))})`);gr.addColorStop(1,`rgb(${art.map(v=>v*.55|0)})`);k.fillStyle=gr;k.fillRect(0,y0,32,y1-y0);
    for(let i=0;i<6;i++){k.fillStyle=`rgba(255,255,255,${.06+.06*rnd(h*50+i)})`;k.fillRect(0,y0+rnd(h*70+i)*(y1-y0),32,6+rnd(i+h)*20);}}k.globalAlpha=1;};
  const titleBand=(y0,y1)=>{const gr=k.createLinearGradient(0,0,32,0);gr.addColorStop(0,'rgba(0,0,0,0)');gr.addColorStop(.3,'rgba(0,0,0,.42)');gr.addColorStop(.7,'rgba(0,0,0,.42)');gr.addColorStop(1,'rgba(0,0,0,0)');k.fillStyle=gr;k.fillRect(0,y0,32,y1-y0);};
  if(style==='jewel'||style==='clam'||style==='case'){
   // authentic region styles: NTSC-U black with a white title; PAL black with a white
   // publisher band at the foot; JP spine card (white, coloured cap, black title). Neutral mark, no logos.
   if(style==='jewel'){
    // Region-specific spine layout: NA black; PAL black or dark blue with PAL under the wordmark and a
    // light foot band; JP the white spine card with the product code at the foot. The wordmark as plain text.
    const jp=rg==='jp',eu=rg==='eu',blue=eu&&h>.66,ink=jp?'#151515':'#f6f6f2';
    const gr=k.createLinearGradient(0,0,0,256);
    if(jp){gr.addColorStop(0,'#f4f4f0');gr.addColorStop(1,'#e0e0d8');}else if(blue){gr.addColorStop(0,'#23397a');gr.addColorStop(1,'#14234f');}else{gr.addColorStop(0,'#1d1d20');gr.addColorStop(1,'#0b0b0c');}
    k.fillStyle=gr;k.fillRect(0,0,32,256);
    k.save();k.translate(16,4);k.rotate(Math.PI/2);k.textBaseline='middle';k.font=`700 7px ${SANS}`;k.fillStyle=ink;k.fillText('PlayStation',0,0);k.restore();
    if(eu){k.font=`800 7px ${SANS}`;k.textAlign='center';k.fillStyle=ink;k.fillText('PAL',16,48);k.textAlign='left';}
    title(0,eu?56:46,eu?160:jp?168:184,ink,null);
    if(eu){k.fillStyle='#d6d6d2';k.fillRect(0,236,32,20);}
    if(jp){k.save();k.translate(16,224);k.rotate(Math.PI/2);k.textBaseline='middle';k.font=`600 5px ${SANS}`;k.fillStyle='#3a3a3a';k.fillText('SLPS-0'+(1000+Math.floor(h*8999)),0,0);k.restore();}
    k.fillStyle=jp?'rgba(0,0,0,.18)':'rgba(255,255,255,.16)';k.fillRect(1,0,1,256);k.fillRect(30,0,1,256);}
   else if(false){k.fillStyle='#ece8e0';k.fillRect(0,0,32,256);const bc=['#c81e2a','#1e4aa8','#e0a010'][Math.floor(h*3)];k.fillStyle=bc;k.fillRect(0,0,32,36);mark(11,'#fff');
    title(0,44,170,'#141414',null);vtext(pub,222,'#3a3a3a',7);k.fillStyle='rgba(0,0,0,.25)';k.fillRect(0,0,2,256);k.fillRect(30,0,2,256);}
   else{k.fillStyle=style==='case'&&S.band==='#d8d8dc'?'#ececee':'#0b0b0d';const light=k.fillStyle==='#ececee';k.fillRect(0,0,32,256);
    if(style==='clam'){k.fillStyle=S.band;k.fillRect(0,0,32,30);vtext(S.short.toUpperCase(),6,S.band==='#d8d8d8'||S.band==='#e0c020'?'#111':'#fff',8);}else{mark(9,light?'#3a3a44':'#f4f4f4');k.fillStyle=light?'#3a3a44':'#f4f4f4';k.fillRect(6,28,20,1);}
    title(0,36,rg==='eu'?168:176,light?'#141414':'#f7f7f4',null);
    if(rg==='eu'&&style==='jewel'){k.fillStyle='#f4f4f4';k.fillRect(0,214,32,42);vtext(pub,218,'#111',7);}else vtext(pub,220,light?'#333':'#c8c8c8',7);
    k.fillStyle='rgba(255,255,255,.14)';k.fillRect(3,0,1,256);}
  }else if(style==='card'&&g.sys==='gba'){
   // Consistent GBA spines: a real spine scan when one is fetched
   // (above), else one solid colour from the game's own cover (the dominant saturated colour), the title, and a
   // small GAME BOY ADVANCE in plain text at the foot
   const c=coverColor(fr)||art,L=.3*c[0]+.59*c[1]+.11*c[2],light=L>150,ink=light?'#141414':'#ffffff';
   const gr=k.createLinearGradient(0,0,0,256);gr.addColorStop(0,`rgb(${c.map(v=>Math.min(255,v*1.08|0))})`);gr.addColorStop(1,`rgb(${c.map(v=>v*.86|0)})`);k.fillStyle=gr;k.fillRect(0,0,32,256);
   if(!light)for(const[dx,dy]of[[-1,0],[1,0],[0,-1],[0,1]]){k.save();k.translate(dx,dy);title(0,20,185,'rgba(0,0,0,.7)',null,17);k.restore();}
   title(0,20,185,ink,null,17);
   k.globalAlpha=.7;k.font=`700 5px ${SANS}`;k.fillStyle=ink;
   for(const[x,t]of[[12,'GAME BOY'],[20,'ADVANCE']]){k.save();k.translate(x,216);k.rotate(Math.PI/2);k.textBaseline='middle';k.fillText(t,0,0);k.restore();}
   k.globalAlpha=1;k.fillStyle='rgba(255,255,255,.22)';k.fillRect(0,0,1,256);k.fillRect(31,0,1,256);
  }else if(style==='card'){   // cardboard: the cover colour, a system band at the top, grain and worn edges
   k.fillStyle=`rgb(${art})`;k.fillRect(0,0,32,256);artStrip(40,256,.55);
   for(let i=0;i<220;i++){k.fillStyle=`rgba(${rnd(i+h*99)>.5?'255,255,255':'0,0,0'},${.05*rnd(i*3+h)})`;k.fillRect(rnd(i*7+h)*32,rnd(i*11+h)*256,2,3);}
   const band=g.sys==='gba'?'#3a2d7c':S.band;k.fillStyle=band;k.fillRect(0,0,32,40);k.fillStyle='rgba(255,255,255,.75)';k.fillRect(0,40,32,1);
   vtext(S.short.toUpperCase(),6,'#f0ecff',8);
   titleBand(46,220);title(0,50,164,'#fbf6ea','rgba(0,0,0,.85)');
   k.fillStyle='#f4f4f0';k.fillRect(8,226,16,22);k.fillStyle='#111';k.fillRect(10,228,12,12);
   k.fillStyle='rgba(0,0,0,.28)';k.fillRect(0,0,2,256);k.fillRect(30,0,2,256);k.fillStyle='rgba(255,255,255,.18)';k.fillRect(0,0,32,1);k.fillRect(0,255,32,1);}
  return SPINE[ck]=cv;
 }
 const MINI={};       // a spine's mean colour (bottom-screen ticks)
 function miniColour(g){const key=g.sys+'/'+g.title;if(MINI[key])return MINI[key];
  const p=canvas(1,1).getContext('2d');p.drawImage(spineCanvas(g),0,60,32,150,0,0,1,1);const d=p.getImageData(0,0,1,1).data;return MINI[key]=`rgb(${d[0]},${d[1]},${d[2]})`;}

 // ---- the far view: the runtime 'shelf' texture (512x256) is baked ONCE per library: one generic spine tile per
 // spine style (128x64, fake titles: just bands and strokes) and one brass plate per section (128x16).
 const SHELFCV=canvas(512,256);let SHELF_T=null,SHELF_KEY=null,STRIP_PAINTS=0;
 const STYLES=['jewel','card','clam','case','mix'];
 function bakeShelf(rm){
  const key=rm.id+'/'+SYSSET;if(SHELF_KEY===key)return false;SHELF_KEY=key;STRIP_PAINTS++;
  const k=SHELFCV.getContext('2d');k.clearRect(0,0,512,256);
  // generic spine tiles: a full section (356 mm) of boxes, bottom-aligned; texel = 356/128 mm
  STYLES.forEach((st,ti)=>{const x0=(ti%4)*128,y0=Math.floor(ti/4)*64+(ti>=4?96:0);let x=0,j=0;
   while(x<128){const s=st==='mix'?['jewel','card'][j%2]:st,T=(s==='jewel'||s==='case'?14:26)/2.78+(rnd(ti*91+j)*.6),H=(s==='card'?135:126)/168*64,h=rnd(ti*37+j*7);
    const body=s==='jewel'?(h<.75?'#0d0d10':h<.9?'#e8e4dc':'#16161a'):s==='clam'?'#111114':s==='case'?(h<.5?'#e6e6ea':'#1a1a1e'):`hsl(${Math.floor(h*360)},${35+h*30}%,${30+h*25}%)`;
    k.fillStyle=body;k.fillRect(x0+x,y0+64-H,T-.3,H);
    const ink=body==='#e8e4dc'||body==='#e6e6ea'?'rgba(20,20,20,.75)':'rgba(240,240,236,.78)';k.fillStyle=ink;k.fillRect(x0+x+T*.38,y0+64-H*.76,Math.max(1,T*.24),H*.5);   // a fake title
    if(s==='card'){k.fillStyle='rgba(40,30,90,.9)';k.fillRect(x0+x,y0+64-H,T-.3,H*.15);}
    if(s==='clam'){k.fillStyle='#b81a1a';k.fillRect(x0+x,y0+64-H,T-.3,H*.12);}
    x+=T+.25;j++;}});
  // brass plates, one per section
  LIBROWS.forEach(sec=>{const x=(sec.i%4)*128,y=64+Math.floor(sec.i/4)*16;
   k.fillStyle='#b8925a';k.fillRect(x,y,128,16);k.fillStyle='#e6c88a';k.fillRect(x,y,128,1);k.fillStyle='#6a4a20';k.fillRect(x,y+15,128,1);
   k.font=`700 11px ${TYPE}`;k.textAlign='center';k.fillStyle='#2a1808';k.fillText(SYS[sec.sid].plate,x+64,y+12);k.textAlign='left';});
  return true;
 }
 const tileOf=sec=>{const st=SYS[sec.sid].style,ti=STYLES.indexOf(st);return[(ti%4)*128,Math.floor(ti/4)*64+(ti>=4?96:0)];};
 // one mesh: a quad per owned section (its generic tile, cropped to how full the section is) + its plate.
 // Palette: 0 normal, 1 highlighted on the wall, 2 the section the camera is in (hidden once its real spines are in)
 let STRIPMESH=null;
 function stripMesh(rm,hsel,cur){
  const key=rm.id+'/'+SYSSET+'/'+hsel+'/'+cur+'/'+OPT.lm;if(STRIPMESH&&STRIPMESH.key===key)return STRIPMESH;
  const C=rm.CASE,V=[],I=[];let n=0,slot=0,uv2=null;
  const vtx=(x,y,z,u,v,l)=>{V.push([x,y,z,u*16,v*16,l,slot,uv2?uv2(x,y):[1,1]]);return n++;};
  const baked=OPT.lm&&rm.f.anchors['slm.0.0'];
  const quad=(x0,x1,y0,y1,z,u0,v0,u1,v1,l)=>{const a=vtx(x0,y1,z,u0,v0,l),b=vtx(x1,y1,z,u1,v0,l),c=vtx(x1,y0,z,u1,v1,l),d=vtx(x0,y0,z,u0,v1,l);I.push(a,c,b,a,d,c);};
  const lit=baked?1.0:rm.id==='night'?.6:rm.id==='apartment'?1.0:.84;   // the baked slabs carry the light
  LIBROWS.forEach(sec=>{slot=sec.i===cur?2:sec.i===hsel?1:0;const[u,v]=tileOf(sec),f=sec.fill,b=sec.base;uv2=baked?(x,y)=>slmUV(rm,sec.r,sec.c,x,y):null;
   const fl=sec.i%2;   // every other section mirrors the tile, so neighbours don't repeat
   quad(sec.xin0,sec.xin0+sec.inner*f,b,b+C.gap-C.board,6,fl?u+128*f:u,v+.02,fl?u:u+128*f,v+64,lit);});
  // the plates: the light at the foot of their section
  LIBROWS.forEach(sec=>{slot=sec.i===hsel?1:0;uv2=baked?(x,y)=>slmUV(rm,sec.r,sec.c,x,sec.base):null;const x=(sec.i%4)*128,v=64+Math.floor(sec.i/4)*16,b=sec.base;
   quad(sec.cx-80,sec.cx+80,b-C.board+3,b-3,-2.5,x+.5,v+.5,x+127.5,v+15.5,1.0);});
  return STRIPMESH={key,m:meshFrom(pack(V,I)),count:I.length};
 }
 // vertices with a lightmap uv (v[7], texels) get the room's 20-byte layout
 function pack(V,I){const st=V.some(v=>v[7])?20:16,buf=new ArrayBuffer(V.length*st),dv=new DataView(buf);
  V.forEach((v,i)=>{const o=i*st;if(st===20){dv.setInt16(o+16,Math.round(v[7][0]*16),true);dv.setInt16(o+18,Math.round(v[7][1]*16),true);}dv.setInt16(o,Math.round(v[0]),true);dv.setInt16(o+2,Math.round(v[1]),true);dv.setInt16(o+4,Math.round(v[2]),true);dv.setInt16(o+6,v[6]||0,true);
   dv.setInt16(o+8,Math.round(v[3]),true);dv.setInt16(o+10,Math.round(v[4]),true);const l=Array.isArray(v[5])?v[5]:[v[5],v[5],v[5]];for(let c=0;c<3;c++)dv.setUint8(o+12+c,clamp(Math.round(l[c]*128),0,255));dv.setUint8(o+15,255);});
  return{vb:new Uint8Array(buf),ib:new Uint16Array(I),stride:st};}

 // ---- real boxes near the camera: a spine ring streamed as you scroll. 32x256 cells (readable
 // titles), 512x512 = 32 cells, cell 31 is plain white (box tops and sides) -> 31 real boxes at most.
 const RCW=32,RCH=256,RCOLS=16,RCELLS=31;
 const RINGCV=canvas(512,512);let RING_T=null;const RING={cell:new Map(),slot:new Array(RCELLS).fill(null),loads:0,dirty:false};
 function ringCell(row,g){
  const key=row.i+'/'+g.i;let c=RING.cell.get(key);if(c!=null)return c;
  let free=RING.slot.indexOf(null);
  if(free<0){let far=-1,fd=-1;RING.slot.forEach((k,i)=>{const[r,j]=k.split('/').map(Number),d=r!==row.i?1e9:Math.abs(j-g.i);if(d>fd){fd=d;far=i;}});free=far;RING.cell.delete(RING.slot[free]);}
  RING.slot[free]=key;RING.cell.set(key,free);RING.loads++;
  const k=RINGCV.getContext('2d'),x=(free%RCOLS)*RCW,y=Math.floor(free/RCOLS)*RCH;k.imageSmoothingEnabled=true;k.imageSmoothingQuality='high';k.drawImage(spineCanvas(g),x,y,RCW,RCH);RING.dirty=true;
  return free;
 }
 let BOXBUF=null;
 function boxMesh(rm,w,row,list){
  const C=rm.CASE,V=[],I=[];let n=0;
  const baked=OPT.lm&&rm.f.anchors['slm.0.0'];
  const vtx=(x,y,z,u,v,c)=>{V.push([x,y,z,u*16,v*16,c,0,baked?slmUV(rm,row.r,row.c,x,y):[1,1]]);return n++;};   // the slab's baked light
  const quad=(W,uv,c)=>{const a=vtx(...W[0],uv[0],uv[1],c),b=vtx(...W[1],uv[2],uv[1],c),d=vtx(...W[2],uv[2],uv[3],c),e=vtx(...W[3],uv[0],uv[3],c);I.push(a,d,b,a,e,d);};
  const lit=baked?1.0:rm.id==='night'?.8:rm.id==='apartment'?1.0:.92,WU=[(RCELLS%RCOLS)*RCW+15.5,Math.floor(RCELLS/RCOLS)*RCH+127.5];   // spines lit a little more (legibility)
  for(const{g,x,dz,hl}of list){
   const cell=ringCell(row,g),cx=(cell%RCOLS)*RCW,cy=Math.floor(cell/RCOLS)*RCH,b=g.b,col=[lit*hl,lit*hl,lit*hl];
   const x0=x-b.T/2,x1=x+b.T/2,y0=row.base,y1=row.base+b.H,z0=4+dz,z1=4+b.W+dz;
   quad([[x0,y1,z0],[x1,y1,z0],[x1,y0,z0],[x0,y0,z0]],[cx+.5,cy+.5,cx+RCW-.5,cy+RCH-.5],col);
   const top=g.sys==='ps1'||g.sys==='md'||g.sys==='nds'?[.16,.16,.18]:[.55,.5,.44];
   quad([[x0,y1,z1],[x1,y1,z1],[x1,y1,z0],[x0,y1,z0]],[WU[0],WU[1],WU[0],WU[1]],top.map(v=>v*lit*hl));
   quad([[x1,y1,z0],[x1,y1,z1],[x1,y0,z1],[x1,y0,z0]],[WU[0],WU[1],WU[0],WU[1]],top.map(v=>v*lit*hl*.7));
   quad([[x0,y1,z1],[x0,y1,z0],[x0,y0,z0],[x0,y0,z1]],[WU[0],WU[1],WU[0],WU[1]],top.map(v=>v*lit*hl*.7));
  }
  const k=RINGCV.getContext('2d');k.fillStyle='#fff';k.fillRect((RCELLS%RCOLS)*RCW,Math.floor(RCELLS/RCOLS)*RCH,RCW,RCH);
  const p=pack(V,I);
  if(!BOXBUF||BOXBUF.stride!==p.stride)BOXBUF=meshFrom(p,true);
  else{gl.bindBuffer(gl.ARRAY_BUFFER,BOXBUF.vb);gl.bufferData(gl.ARRAY_BUFFER,p.vb,gl.DYNAMIC_DRAW);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,BOXBUF.ib);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,p.ib,gl.DYNAMIC_DRAW);}
  return{m:BOXBUF,count:I.length,boxes:list.length};
 }

 // ------------------------------------------------------------------ props (the held box / the case that opens)
 let PROPS=null;const CASECV=canvas(256,256);let CASE_T=null,CASE_KEY=null;
 function props(){if(PROPS)return PROPS;initGL();const f=parse(window.ROOM3D.props),m=meshFrom(f),parts={};f.parts.forEach(p=>parts[p.name]=p);CASE_T=texFrom(256,256,null,0);return PROPS={f,m,parts};}
 // Authentic case backs: when we have no real back scan, a plain back in the real style instead
 // of the library card: GBA cardboard back (the cover's colour, the system band down the left with GAME BOY
 // ADVANCE, the title, two screens cut from the cover art, blurb lines); PS1 jewel insert (dark, the spine strips
 // at both edges, the title, two screens, blurb lines). No codes or numbers.
 const BACKS={};
 function authenticBack(g,tx){const key=g.sys+'/'+g.title;if(BACKS[key])return BACKS[key];
  const W=256,H=256,cv=canvas(W,H),k=cv.getContext('2d'),fr=tx.front,c0=coverColor(fr)||[60,70,110],gba=g.sys==='gba';
  const bg=gba?c0.map(v=>v*.55+20|0):[22,23,27],lite=c0.map(v=>Math.min(255,v*1.25+30|0));
  k.fillStyle=`rgb(${bg})`;k.fillRect(0,0,W,H);
  const gr=k.createLinearGradient(0,0,0,H);gr.addColorStop(0,'rgba(255,255,255,.08)');gr.addColorStop(1,'rgba(0,0,0,.25)');k.fillStyle=gr;k.fillRect(0,0,W,H);
  let x0=10,x1=W-10;
  if(gba){const band=30;const bgr=k.createLinearGradient(0,0,band,0);bgr.addColorStop(0,'#2a1f5c');bgr.addColorStop(1,'#4a3a8e');k.fillStyle=bgr;k.fillRect(0,0,band,H);
   k.save();k.translate(band*.66,H-10);k.rotate(-Math.PI/2);k.font=`800 14px ${SANS}`;k.fillStyle='#e8e2ff';k.fillText('GAME BOY ADVANCE',0,0);k.restore();x0=band+10;}
  else{for(const x of[0,W-16]){k.fillStyle='#0c0c0e';k.fillRect(x,0,16,H);k.fillStyle='rgba(255,255,255,.14)';k.fillRect(x+5,0,1,H);}x0=26;x1=W-26;
   k.save();k.translate(8,H/2);k.rotate(Math.PI/2);k.textAlign='center';k.textBaseline='middle';k.font=`700 8px ${SANS}`;k.fillStyle='#e8e8e8';k.fillText(g.title,0,0);k.restore();}
  k.fillStyle='#fff';k.textBaseline='top';const z=R8.fitFont(k,g.title,x1-x0,22,SANS);k.font=`800 ${z}px ${SANS}`;k.fillText(g.title,x0,14);
  k.fillStyle=`rgb(${lite})`;k.fillRect(x0,14+z+6,x1-x0,2);
  // two screens cut from the cover art, white-framed
  const sw=(x1-x0-10)/2,sh=sw*.67,sy=14+z+16;
  [[.15,.25],[.55,.55]].forEach(([u,v],i)=>{const sx=x0+i*(sw+10);k.fillStyle='#f4f4f0';k.fillRect(sx-2,sy-2,sw+4,sh+4);
   k.drawImage(fr,fr.width*u,fr.height*v,fr.width*.32,fr.height*.24,sx,sy,sw,sh);});
  // the blurb: lines, not text
  for(let i=0;i<7;i++){const y=sy+sh+16+i*9;if(y>H-34)break;k.fillStyle='rgba(255,255,255,.55)';k.fillRect(x0,y,(x1-x0)*(i===6?.55:.92+.06*Math.sin(i*2.3)),3);}
  // the foot: a darker strip, a blank rating box, small marks (no codes, no numbers)
  k.fillStyle='rgba(0,0,0,.35)';k.fillRect(gba?30:16,H-26,W-(gba?30:32),26);k.fillStyle='#f4f4f0';k.fillRect(x0,H-22,14,18);k.fillStyle='#222';k.font=`800 12px ${SANS}`;k.textBaseline='middle';k.fillText('E',x0+3,H-13);
  for(let i=0;i<3;i++){k.fillStyle='rgba(255,255,255,.5)';k.fillRect(x0+24,H-20+i*6,(x1-x0)*.5,2);}
  return BACKS[key]=cv;}
 function frontOf(g){if(g.sys==='ps1'||g.sys==='gba'){const tx=R8.boxTex(R8.game(g.sys,g.title)),sm=(window.BOXFLIP_SAMPLES||{})[g.sys+'/'+g.title],real=sm&&sm.back&&App.images[sm.back]&&App.images[sm.back].naturalWidth;
   return real?tx:{...tx,back:authenticBack(g,tx)};}
  const s=spineCanvas(g),f=canvas(128,128),k=f.getContext('2d');k.fillStyle=SYS[g.sys].band;k.fillRect(0,0,128,128);k.drawImage(s,0,0,s.width,s.height,16,10,96,108);
  k.fillStyle='#f4eee0';k.font=`800 13px ${SANS}`;k.textAlign='center';k.fillText(SYS[g.sys].plate,64,124);return{front:f,back:f,spine:s};}
 function caseTexture(g){
  props();const key=g.sys+"/"+g.title+"/"+OPT.spines;if(CASE_KEY===key)return;CASE_KEY=key;
  const k=CASECV.getContext('2d'),tx=frontOf(g);k.imageSmoothingEnabled=true;k.clearRect(0,0,256,256);
  k.drawImage(tx.front,0,0,128,128);k.drawImage(tx.back,128,0,128,128);k.drawImage(spineCanvas(g),0,128,16,128);
  k.fillStyle='#fff';k.fillRect(16,128,16,16);
  k.save();k.beginPath();k.arc(192,192,63,0,Math.PI*2);k.clip();k.fillStyle='#c8ccd4';k.fillRect(128,128,128,128);
  k.globalAlpha=.92;k.drawImage(tx.front,136,136,112,112);k.globalAlpha=1;
  const sheen=k.createLinearGradient(140,140,244,244);sheen.addColorStop(0,'rgba(255,255,255,.35)');sheen.addColorStop(.5,'rgba(160,220,255,.08)');sheen.addColorStop(1,'rgba(255,200,255,.3)');k.fillStyle=sheen;k.fillRect(128,128,128,128);
  k.fillStyle='#9ea4ae';k.beginPath();k.arc(192,192,22,0,Math.PI*2);k.fill();k.fillStyle='#000';k.beginPath();k.arc(192,192,8,0,Math.PI*2);k.fill();k.restore();
  k.fillStyle='#2a2a30';k.fillRect(32,128,64,64);k.drawImage(tx.front,36,132,56,48);k.fillStyle='#ddd';k.fillRect(36,182,56,6);
  k.fillStyle='#c9b48e';k.fillRect(32,192,32,32);
  canvasTex(CASE_T,CASECV,true);
 }

 // ------------------------------------------------------------------ the TV picture
 const TVC=canvas(128,128),THUMB=canvas(128,96);let TV_KEY=-1;
 if(window.ROOM3D_THUMB){const b=atob(window.ROOM3D_THUMB),k=THUMB.getContext('2d'),im=k.createImageData(128,96);for(let i=0;i<128*96;i++){for(let c=0;c<3;c++)im.data[i*4+c]=b.charCodeAt(i*3+c);im.data[i*4+3]=255;}k.putImageData(im,0,0);}
 function tvPicture(k,t,x0,w){
  k.imageSmoothingEnabled=false;k.fillStyle='#000';k.fillRect(0,0,128,128);
  k.drawImage(THUMB,x0,0,w,96);
  const n=Math.floor(t*15);for(let i=0;i<70;i++){k.fillStyle=`rgba(255,255,255,${rnd(i+n*7)*.08})`;k.fillRect(rnd(i*3+n)*128,rnd(i*5+n)*96,1,1);}
  k.fillStyle='rgba(0,0,0,.4)';k.fillRect(0,38,128,20);k.fillStyle='#fff';k.font=`700 11px ${R8.SANS}`;k.textAlign='center';k.fillText('PAUSE',64,52);k.textAlign='left';
  for(let y=1;y<96;y+=2){k.fillStyle='rgba(0,0,0,.16)';k.fillRect(0,y,128,1);}
 }
 function tvTexture(rm,t){
  const fr=Math.floor(t*15);if(TV_KEY===fr+rm.id)return;TV_KEY=fr+rm.id;
  const k=TVC.getContext('2d');if(rm.A.screen[3]>.5)tvPicture(k,t,18,92);else tvPicture(k,t,0,128);
  canvasTex(rm.T[1],TVC);
  const c9=R9.TVCV.getContext('2d');c9.imageSmoothingEnabled=false;c9.drawImage(TVC,0,0,128,96,0,0,256,192);
 }

 // ------------------------------------------------------------------ drawing
 let STATS=null;
 function bindMesh(P,m){
  gl.bindBuffer(gl.ARRAY_BUFFER,m.vb);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,m.ib);
  gl.enableVertexAttribArray(P.a.aPos);gl.vertexAttribPointer(P.a.aPos,4,gl.SHORT,false,16,0);
  gl.enableVertexAttribArray(P.a.aUV);gl.vertexAttribPointer(P.a.aUV,2,gl.SHORT,false,16,8);
  gl.enableVertexAttribArray(P.a.aCol);gl.vertexAttribPointer(P.a.aCol,4,gl.UNSIGNED_BYTE,true,16,12);
  // the room's 20-byte vertices carry the lightmap uv; every other mesh points at the neutral texel
  if(m.stride===20){const st=20;gl.vertexAttribPointer(P.a.aPos,4,gl.SHORT,false,st,0);gl.vertexAttribPointer(P.a.aUV,2,gl.SHORT,false,st,8);gl.vertexAttribPointer(P.a.aCol,4,gl.UNSIGNED_BYTE,true,st,12);
   gl.enableVertexAttribArray(P.a.aUV2);gl.vertexAttribPointer(P.a.aUV2,2,gl.SHORT,false,st,16);}
  else{gl.disableVertexAttribArray(P.a.aUV2);gl.vertexAttrib2f(P.a.aUV2,16,16);}
 }
 function draw(P,T,first,count,o={}){
  if(!count)return;const u=P.u;
  gl.uniformMatrix4fv(u.uM,false,o.M||I4);gl.uniform1f(u.uMode,o.mode||0);
  const pal=o.pal||PAL0;gl.uniformMatrix4fv(u['uPal[0]'],false,pal.M);gl.uniform3fv(u['uTintP[0]'],pal.T);gl.uniform1f(u.uKind,o.kind||0);
  gl.uniform1f(u.uLmOn,o.lm&&OPT.lm?(OPT.dbg==='lm'?2:OPT.dbg==='uv2'?3:1):0);gl.uniform3fv(u.uTint,o.tint||[1,1,1]);gl.uniform1f(u.uFade,o.fade==null?1:o.fade);gl.uniform3fv(u.uSkin,o.skin||[1,1,1]);gl.uniform2fv(u.uGlove,o.glove||[0,0]);
  gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,T.t);gl.uniform2f(u.uTexSize,T.w,T.h);
  gl.drawElements(gl.TRIANGLES,count,gl.UNSIGNED_SHORT,first*2);
  if(STATS){if(o.stat!==false)STATS.draws++;STATS.tris+=count/3;}
 }
 const NPAL=20,PAL0={M:new Float32Array(NPAL*16).map((_,i)=>i%16%5===0?1:0),T:new Float32Array(NPAL*3).fill(1)};   // 20 slots: the room's 12, the hand's 17
 function palette(list){const M=new Float32Array(NPAL*16),T=new Float32Array(NPAL*3).fill(1);M.set(PAL0.M);list.forEach((m,i)=>{if(m)M.set(m.M||m,i*16);if(m&&m.T)T.set(m.T,i*3);});return{M,T};}
 function begin(cam,t){
  POST_T=t;const P=P0,u=P.u;gl.useProgram(P.p);gl.uniformMatrix4fv(u.uVP,false,cam.VP);gl.uniform3fv(u.uRight,cam.R);gl.uniform3fv(u.uUp,cam.U);gl.uniform1f(u.uTime,t);
  gl.uniform3fv(u.uCam,cam.eyeW);gl.uniform3fv(u.uCamV,cam.eyeW);gl.uniform3fv(u.uKey,R8.KEY);
  gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);gl.enable(gl.CULL_FACE);gl.cullFace(gl.BACK);gl.frontFace(gl.CW);
  return P;
 }
 function scopeUniforms(P,rm,w){
  const u=P.u,S=rm.A.scope,L=rm.A.slabel,sc=w.scope,mix=clamp((w.t-sc.t0)/.35);
  gl.uniform3f(u.uSc0,S[0],S[1],S[2]);gl.uniform3f(u.uScH,S[3],S[4],S[5]);gl.uniform4f(u.uScT,sc.prev,sc.cur,ease(mix),sc.cur);
  gl.uniform4f(u.uScP,S[6],S[9],S[7],0);gl.uniform2f(u.uLab,OPT.nolab?0:L[3],OPT.nolab?0:L[4]);
 }
 // the room (+ the library strips/boxes); `o.hideLib` leaves the boxes out (zoomed far away), `o.only` = 'card'
 // the room's Cycles lightmap (texture unit 1, bilinear by default, nearest with OPT.lmNearest)
 function lightmapBind(P,rm){const i=rm.f.tex.findIndex(t=>t.name==='light');if(i<0){OPT.lm=0;return;}const T=rm.T[i];
  gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,T.t);const f=OPT.lmNearest?gl.NEAREST:gl.LINEAR;
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,f);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,f);
  gl.uniform1i(P.u.uLm,1);gl.uniform2f(P.u.uLmSize,T.w,T.h);gl.activeTexture(gl.TEXTURE0);}
 // light probes: the ambient cube nearest the point (inverse-distance blend of the 4 nearest), seen along n
 // (a unit vector, e.g. towards the camera); x2 = the lightmap's scale. Lights the moving pieces.
 function probe(rm,p,n,floor=0){const P_=rm.f.probes;if(!P_||!P_.length||!OPT.lm)return[1,1,1];
  // probes buried in furniture (all six faces dark) are skipped
  const near=P_.filter(q=>q.c.reduce((a,b)=>a+b,0)>.6).map(q=>[q,Math.hypot(q.p[0]-p[0],q.p[1]-p[1],q.p[2]-p[2])]).sort((a,b)=>a[1]-b[1]).slice(0,4);let ws=0;const c=[0,0,0];
  if(!near.length)return[1,1,1];
  for(const[q,d]of near){const w=1/(d+60);ws+=w;const ax=[[n[0]>0?0:1,n[0]*n[0]],[n[1]>0?2:3,n[1]*n[1]],[n[2]>0?4:5,n[2]*n[2]]];
   for(const[f,k]of ax)for(let j=0;j<3;j++)c[j]+=w*k*q.c[f*3+j];}
  // floor: what must stay readable (box art, spines) never goes below it; the colour of the light is kept
  const r=c.map(v=>2*v/ws),m=Math.max(...r,1e-3);return m>=floor?r:r.map(v=>v*floor/m);}
 const tintMul=(a,b)=>a.map((v,i)=>v*b[i]);
 // Lighting: the games stay white: box art takes the light's level, little of its colour
 const neutral=(c,keep=.25)=>{const L=.299*c[0]+.587*c[1]+.114*c[2];return c.map(v=>L+(v-L)*keep);};
 // Lighting: the hands carry a small warm fill of their own, so the night room's cool light doesn't turn
 // them grey-blue (part neutral, plus the fill, then the readability floor)
 const HANDFILL={night:[.16,.11,.05],rustic:[.06,.045,.03],apartment:[.04,.035,.03],nineties:[.06,.04,.05]};
 function handLight(rm,p,n){const f=HANDFILL[rm.id]||[0,0,0],h=neutral(probe(rm,p,n),.5).map((v,i)=>v+f[i]),m=Math.max(...h);return m>=.62?h:h.map(v=>v*.62/m);}
 // the shelf sections' baked light (lightbake.py slabs): anchor slm.<row>.<bay> = the cell's top-left
 // lightmap texel, then the texel steps along x (left -> right) and along y (top -> bottom); [1,1] = neutral
 function slmUV(rm,r,k,x,y){const C=rm.CASE,a=OPT.lm&&rm.f.anchors[`slm.${r}.${k}`];if(!a)return[1,1];
  const x0=C.x0+k*C.bay+C.upr/2,x1=x0+C.bay-C.upr,y0=r*C.gap,y1=y0+C.gap-C.board,fx=clamp((x-x0)/(x1-x0),0,1),fy=clamp((y1-y)/(y1-y0),0,1);
  return[a[0]+fx*a[2]+fy*a[4],a[1]+fx*a[3]+fy*a[5]];}
 function scene(rm,w,cam,o={}){
  const t=w.t,P=begin(cam,t);
  bindMesh(P,rm.m);
  const T=rm.T[0],pp=rm.parts,A=rm.A;
  const tint=g=>{let k=1;const pl=w.mode==='home'?w.focus:w.place;
   if(pl&&GROUP[pl]===g){const f=w.mode==='home'?.22+.08*Math.sin(t*3.2):w.mode==='go'?.16*smooth((t-w.t0)/GLIDE):w.mode==='back'?.16*(1-smooth((t-w.t0)/GLIDE)):0;k+=f;
    return[k*1.02,k,k*.94];}
   return[1,1,1];};
  gl.disable(gl.BLEND);gl.depthMask(true);
  // the palette: one matrix and one tint per slot (static, five places, six moving pieces)
  const hm=w.clock||[21,47],M=[],TT=[],put=(i,m,tn)=>{M[i]=m;TT[i]=tn||[1,1,1];};
  put(0,I4);['p_tv','p_lib','p_set','p_tog','p_share'].forEach((g,i)=>put(i+1,I4,tint(g)));
  put(6,about(pp.a_clockh.pivot,rotZ(-(hm[0]%12+hm[1]/60)*Math.PI/6)));put(7,about(pp.a_clockm.pivot,rotZ(-hm[1]*Math.PI/30)));
  put(8,about(pp.a_knob.pivot,rotZ(-(w.tune-(NCH-1)/2)*A.scope[7])),tint('p_set'));
  for(let i=0;i<2;i++){const p=pp['a_ctrl'+i],l=w.tog.lift[i];put(9+i,mul(T4(0,l*90,-l*40),about(p.pivot,rotX(-l*.5))),tint('p_tog').map(v=>v*(1+.25*l)));}
  put(11,w.share.p>.002?HIDE:cardMatrix(rm,w,cam),tint('p_share'));
  const pal={M:new Float32Array(M.flat()),T:new Float32Array(TT.flat())};
  lightmapBind(P,rm);
  draw(P,T,pp.static.first,pp.a_card.first+pp.a_card.count-pp.static.first,{pal,lm:true});
  // the TV picture (its own small texture)
  if(!o.libOnly){tvTexture(rm,t);draw(P,rm.T[1],pp.tv_screen.first,pp.tv_screen.count,{tint:tint('p_tv').map(v=>v*(.94+.06*Math.sin(t*17)*Math.sin(t*3.1)))});}
  // the bookcase: wall strips + plates (one draw), then the real boxes near the camera
  libraryDraw(P,rm,w,tint('p_lib'));
  bindMesh(P,rm.m);
  // blob shadows, then the additive sprites (glows, dust, steam and the scope trace: one draw)
  gl.enable(gl.BLEND);gl.depthMask(false);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);
  draw(P,T,pp.shadow.first,pp.shadow.count,{kind:1});
  gl.disable(gl.CULL_FACE);gl.blendFunc(gl.ONE,gl.ONE);
  scopeUniforms(P,rm,w);
  // the god rays (part 'rays', just before 'glow') only in the room view: they fade out on the glide to a
  // place and back in on the way home (a second draw only during the glide); close-ups don't pay their fill
  const rk=!pp.rays?0:w.mode==='home'?1:w.mode==='go'?1-smooth((t-w.t0)/GLIDE):w.mode==='back'?smooth((t-w.t0)/GLIDE):0;
  if(rk>.001&&rk<.999)draw(P,T,pp.rays.first,pp.rays.count,{kind:2,mode:1,fade:rk});
  const s0=rk>=.999?pp.rays.first:pp.glow.first,s1=(o.dust===false?pp.glow.first+pp.glow.count:pp.trace.first+pp.trace.count);
  draw(P,T,s0,s1-s0,{kind:2,mode:1});
  if(o.dust===false)draw(P,T,pp.trace.first,pp.trace.count,{kind:2,mode:1,stat:false});
  gl.depthMask(true);gl.disable(gl.BLEND);gl.enable(gl.CULL_FACE);
 }
 // render one image into GLC (scene into the 15-bit FBO, then the post pass), return GLC
 function render(fn,clear=[0,0,0,1],post=true){
  gl.bindFramebuffer(gl.FRAMEBUFFER,post?FBO:null);gl.viewport(0,0,400,240);
  gl.clearColor(...clear);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);fn();
  if(post){gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,400,240);gl.disable(gl.DEPTH_TEST);gl.disable(gl.BLEND);gl.disable(gl.CULL_FACE);
   gl.useProgram(PP.p);gl.bindBuffer(gl.ARRAY_BUFFER,QUAD);for(let i=0;i<4;i++)gl.disableVertexAttribArray(i);gl.enableVertexAttribArray(PP.a.aP);gl.vertexAttribPointer(PP.a.aP,2,gl.FLOAT,false,0,0);
   gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,FTEX);gl.uniform1f(PP.u.uDither,OPT.dither);gl.uniform1f(PP.u.uVig,OPT.vig?.26:0);gl.uniform1f(PP.u.uGrain,OPT.grain?.045:0);gl.uniform1f(PP.u.uT,POST_T);gl.uniform1f(PP.u.uOn,OPT.ps1?1:0);gl.drawArrays(gl.TRIANGLE_STRIP,0,4);}
  return GLC;
 }
 const OPT={ps1:true,dither:1.0,skin:'2',glove:0,spines:'authentic',lm:1,lmNearest:0,vig:1,grain:1};let POST_T=0;   // lm: the Cycles lightmap   // spines: authentic region styles

 // ---- the library in the room. Far: one draw of pre-baked quads (a generic tile per section + plates).
 // Close: the section the camera glides into gets its real boxes (actual spines, readable titles), cross-faded in
 // over the generic tile as the camera arrives; the tile is then hidden (palette slot 2).
 const realFade=L=>smooth((L.zoom-.3)/.4);
 function libraryDraw(P,rm,w,tn){
  library(rm);const L=w.L,C=rm.CASE;
  if(!SHELF_T)SHELF_T=texFrom(512,256,null,0);
  if(bakeShelf(rm))canvasTex(SHELF_T,SHELFCV,true);
  if(!RING_T)RING_T=texFrom(512,512,null,0);
  const cur=LIBROWS[L.row],fade=realFade(L),inLib=w.mode==='lib'||(w.mode!=='home'&&w.place==='library');
  const wallK=w.mode==='lib'?1-smooth(L.zoom):w.mode==='go'&&w.place==='library'?smooth((w.t-w.t0)/GLIDE):w.mode==='back'&&w.place==='library'?1-smooth((w.t-w.t0)/GLIDE):0;
  const S=stripMesh(rm,inLib&&L.zoom<.98?L.hsel:-1,inLib&&L.zoom>.001?L.row:-1);
  lightmapBind(P,rm);const baked=OPT.lm&&rm.f.anchors['slm.0.0'];   // strips and real boxes sample their slab's baked light
  if(!baked){const rw=LIBROWS[L.row]||{cx:C.x0+C.bay*1.5,base:C.gap*3},pc=[rw.cx,rw.base+90,-60];tn=tintMul(tn,neutral(probe(rm,pc,[0,0,-1],.75)));}
  const pal=palette([{M:I4,T:[1-.25*wallK,1-.25*wallK,1-.22*wallK]},{M:I4,T:[1+.38*wallK,1+.3*wallK,1+.18*wallK]},{M:fade>.99?T4(0,0,4000):I4,T:[1,1,1]}]);
  bindMesh(P,S.m);gl.disable(gl.CULL_FACE);draw(P,SHELF_T,0,S.count,{tint:tn,pal,lm:baked});gl.enable(gl.CULL_FACE);
  if(fade<=.001||!cur)return;
  // real boxes: the current section only (every box inside its uprights; at most 31)
  const off=secOff(cur,L.lcam),list=[];
  for(const g of cur.games){const x=boxX(cur,g,off);if(x<cur.xl||x>cur.xr)continue;if(L.p>.002&&g.i===cur.sel)continue;
   const near=g.i===cur.sel?smooth(1-Math.abs(L.cur-g.i)*3)*smooth(L.zoom)*(1-L.p):0;
   list.push({g,x,dz:-TIP*near,hl:1+.35*near,d:Math.abs(g.i-L.cur)});}
  list.sort((a,b)=>a.d-b.d);list.length=Math.min(list.length,RCELLS);
  const B=boxMesh(rm,w,cur,list);
  if(RING.dirty){canvasTex(RING_T,RINGCV,true);RING.dirty=false;}
  bindMesh(P,B.m);gl.disable(gl.CULL_FACE);
  if(fade<.999){gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);}
  draw(P,RING_T,0,B.count,{tint:tn,kind:fade<.999?5:0,fade,lm:baked});
  gl.disable(gl.BLEND);gl.enable(gl.CULL_FACE);
  if(STATS)STATS.boxes=B.boxes;
 }
 // the wall's section labels (2D, like Citro2D text on the 3DS): readable at the wall view, fade as the camera closes in
 function wallLabels(c,w,cam){
  const L=w.L,inLib=w.mode==='lib'||(w.mode!=='home'&&w.place==='library');if(!inLib)return;
  const a=(w.mode==='lib'?1-smooth(L.zoom/.5):w.mode==='go'?smooth((w.t-w.t0)/GLIDE*1.4-.4):1-smooth((w.t-w.t0)/GLIDE*2));if(a<=.01)return;
  const C=w.rm.CASE;c.save();c.globalAlpha=a;c.textAlign='center';
  for(const sec of LIBROWS){const p=cam.proj(sec.cx,sec.base+C.gap-C.board-14,-8);if(p[2]<=NEAR||p[1]<-10||p[1]>250)continue;
   const on=sec.i===L.hsel,S=SYS[sec.sid],label=S.short,n=sec.games.length;
   c.font=`700 ${on?12:10}px ${SANS}`;const tw=c.measureText(label).width+10;
   c.fillStyle=on?'rgba(255,236,196,.96)':'rgba(24,14,6,.72)';R8.rr(c,p[0]-tw/2,p[1]-8,tw,on?15:13,3);c.fill();
   c.fillStyle=on?'#2a1606':'#f4e6c8';c.fillText(label,p[0],p[1]+(on?3.5:2.5));
   if(on){c.font=`700 9px ${TYPE}`;c.fillStyle='rgba(255,236,196,.95)';c.fillText(n+' games',p[0],p[1]+18);}}
  c.restore();
 }

 // ---- the hands: an articulated rig (js/room3d-hands.js). 17 bones per hand (sleeve, palm, three
 // joints per finger and thumb), skinned in the vertex shader (one palette slot per vertex). Grips are solved
 // against the held object (js/room3d-grips.js: every joint curls until its bone rests on a surface; the max
 // penetration is reported per frame) and every hand moment blends between poses (eased). The left hand is the
 // same mesh drawn with mirrored frames (det -1), its own draw. A hand: {M: the palm bone's matrix (world),
 // pose, slide (mm, back along the forearm: in/out of the picture), ref + twist (the sleeve turns less),
 // chk: {boxes} (world-space boxes for the contact check)}.
 const RH=window.RoomHands,RG=window.RoomGrips;
 const HAND_WRIST=nrm([.34,-.46,.82]);
 const GLOVES=['bare','knit','leather','fingerless','work','mitten'];
 const GLOVE_NAME={bare:'bare hands',knit:'knit',leather:'leather',fingerless:'fingerless',work:'work gloves',mitten:'mittens'};
 const GLOVE_OFF=[[0,0],[128,0],[256,0],[0,128],[128,128],[256,128]];      // texel offset of each glove tile (hand.py UTILE, 64x64 unwrap tiles)
 const SKIN_NAME=['light','medium','dark','deep'];
 function rotAx(ax,a){const[x,y,z]=nrm(ax),c=Math.cos(a),s=Math.sin(a),t=1-c;
  return[t*x*x+c,t*x*y+s*z,t*x*z-s*y,0, t*x*y-s*z,t*y*y+c,t*y*z+s*x,0, t*x*z+s*y,t*y*z-s*x,t*z*z+c,0, 0,0,0,1];}
 const frameMat=F=>{const M=M3(F.ex,F.ey,F.ez);M[12]=F.o[0];M[13]=F.o[1];M[14]=F.o[2];return M;};
 const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
 const handed=F=>Math.sign(dot(cross(F.ex,F.ey),F.ez))||1;
 // blend two frames (small rotations); keeps A's handedness
 function blendF(A,B,k){if(k<=0)return A;if(k>=1)return B;const L3=(a,b)=>[0,1,2].map(i=>lerp(a[i],b[i],k));
  const ex=nrm(L3(A.ex,B.ex));let ey=L3(A.ey,B.ey);const d=dot(ey,ex);ey=nrm(ey.map((v,i)=>v-ex[i]*d));
  return{o:L3(A.o,B.o),ex,ey,ez:cross(ex,ey).map(v=>v*handed(A))};}
 // a frame given in an object's local space (box, lid, cart, card) -> world
 function local(Mo,F){const R=v=>nrm([0,1,2].map(i=>Mo[i]*v[0]+Mo[4+i]*v[1]+Mo[8+i]*v[2]));return{o:xf(Mo,F.o),ex:R(F.ex),ey:R(F.ey),ez:R(F.ez)};}
 const gripR=(b,gy)=>({o:[0,b.H*gy,b.W/2],ex:[1,0,0],ey:[0,1,0],ez:[0,0,1]});
 // matrices: rotation slerp (quaternions), translation lerp; a mirrored pair stays mirrored
 const MZ=[1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1];
 const det3=M=>M[0]*(M[5]*M[10]-M[9]*M[6])-M[4]*(M[1]*M[10]-M[9]*M[2])+M[8]*(M[1]*M[6]-M[5]*M[2]);
 function m2q(M){const t=M[0]+M[5]+M[10];let q;
  if(t>0){const s=Math.sqrt(t+1)*2;q=[(M[6]-M[9])/s,(M[8]-M[2])/s,(M[1]-M[4])/s,s/4];}
  else if(M[0]>M[5]&&M[0]>M[10]){const s=Math.sqrt(1+M[0]-M[5]-M[10])*2;q=[s/4,(M[4]+M[1])/s,(M[8]+M[2])/s,(M[6]-M[9])/s];}
  else if(M[5]>M[10]){const s=Math.sqrt(1+M[5]-M[0]-M[10])*2;q=[(M[4]+M[1])/s,s/4,(M[9]+M[6])/s,(M[8]-M[2])/s];}
  else{const s=Math.sqrt(1+M[10]-M[0]-M[5])*2;q=[(M[8]+M[2])/s,(M[9]+M[6])/s,s/4,(M[1]-M[4])/s];}return q;}
 function q2m(q,t){const[x,y,z,w]=q;return[1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,t[0],t[1],t[2],1];}
 function blendM(A,B,k){if(k<=0)return A;if(k>=1)return B;const mir=det3(A)<0,a=mir?mul(A,MZ):A,b=mir?mul(B,MZ):B;
  const qa=m2q(a),qb=m2q(b);let d=qa[0]*qb[0]+qa[1]*qb[1]+qa[2]*qb[2]+qa[3]*qb[3];const sg=d<0?-1:1;d*=sg;
  let ka=1-k,kb=k*sg;if(d<.9995){const o=Math.acos(d),s=Math.sin(o);ka=Math.sin((1-k)*o)/s;kb=Math.sin(k*o)/s*sg;}
  const q=nrm4([0,1,2,3].map(i=>qa[i]*ka+qb[i]*kb)),M=q2m(q,[0,1,2].map(i=>lerp(A[12+i],B[12+i],k)));return mir?mul(M,MZ):M;}
 const nrm4=v=>{const l=Math.hypot(...v)||1;return v.map(x=>x/l);};
 const rigOf=rm=>rm.rig||(rm.rig=RH.rig(rm.f));
 const isMitt=()=>GLOVES[OPT.glove]==='mitten';
 // solved grips, cached per object size and options: {B (base, object space), pose, ap (approach), spec}
 const GRIPS={};
 function grip(rm,name,b,o={}){const k=[rm.id,name,b.T,b.H,b.W,isMitt()?1:0,JSON.stringify(o)].join('|');
  if(!GRIPS[k]){const sp=RG.spec(RH,name,b,Object.assign({mitten:isMitt()},o)),r=RH.solve(rigOf(rm),sp);GRIPS[k]={B:r.base,pose:r.pose,ap:sp.approach,spec:sp,contact:r.contact,trail:r.trail,mitten:isMitt()};}
  return GRIPS[k];}
 // a hand state in an object's space: base matrix + pose; helpers to back it off along its approach, mirror it,
 // turn it with the object (Mo = object -> its new place) and blend states (eased by the caller)
 const st=(g,pose)=>({B:g.B,pose:pose||g.pose,ap:g.ap,g});
 const backOff=(s,d,pose)=>({...s,B:mul(T4(-s.ap[0]*d,-s.ap[1]*d,-s.ap[2]*d),s.B),pose:pose||s.pose});
 // a mix between a grip and its own approach pose (same base) replays the solver's curl; anything else blends staged
 const mixHandPose=(a,b,k)=>b.preOf&&b.preOf===a.g?RH.curlPose(b.pose,a.g,1-k):a.preOf&&a.preOf===b.g?RH.curlPose(a.pose,b.g,k):RH.stagePose(a.pose,b.pose,k);
 const mixS=(a,b,k)=>({B:blendM(a.B,b.B,k),pose:mixHandPose(a,b,k),ap:a.ap});
 const moveS=(s,M)=>({...s,B:mul(M,s.B),ap:nrm([0,1,2].map(i=>M[i]*s.ap[0]+M[4+i]*s.ap[1]+M[8+i]*s.ap[2]))});
 // a path through hand states [s0, s1, ...] with relative durations, u in 0..1: ONE ease over the whole
 // path (it starts and ends slowly, no stop at the waypoints); the wrist travels on a smooth curve through the
 // waypoints (cubic Hermite, Catmull-Rom tangents: arcs, no corners; a waypoint where the hand rests in place
 // has no tangent, so the hand never drifts while it holds on); the rotation turns per segment; the fingers run
 // a little behind the wrist (overlapping action)
 let PATHDBG=null;
 // Smooth motion: 'arc' paths are retimed by their real motion. The path is
 // sampled through the rig (every hand vertex), the largest vertex move between samples is summed, and time maps
 // to that distance through one smoothstep: the fastest point of the hand moves at an even, eased speed (peak 1.5x
 // the mean), whatever mix of wrist travel, wrist turn and finger curl the path has. Built once per path.
 function pathS(list,u,w){
  if(w!=='arc')return pathAt(list,smooth(clamp(u)),w);
  const T=list._tab||(list._tab=pathTable(list)),d=smooth(clamp(u))*T.D[T.N];
  let j=0;while(j<T.N-1&&T.D[j+1]<d)j++;
  const f=T.D[j+1]>T.D[j]?(d-T.D[j])/(T.D[j+1]-T.D[j]):0;
  return pathAt(list,(j+clamp(f))/T.N,'arc');
 }
 function pathTable(list,N=240){
  const R=rigOf(ROOMS[Object.keys(ROOMS)[0]]),Sm=isMitt()?R.mitt:R.bare,V=Sm.V,D=new Float32Array(N+1);let prev=null;
  for(let j=0;j<=N;j++){const st_=pathAt(list,j/N,'arc'),M=RH.fk(R,st_.B,st_.pose),P=new Float32Array(Sm.nv*3);
   for(let i=0;i<Sm.nv;i++){const m=M[Sm.slot[i]],x=V[i*4],y=V[i*4+1],z=V[i*4+2];P[i*3]=m[0]*x+m[4]*y+m[8]*z+m[12];P[i*3+1]=m[1]*x+m[5]*y+m[9]*z+m[13];P[i*3+2]=m[2]*x+m[6]*y+m[10]*z+m[14];}
   let mx=0;if(prev)for(let i=0;i<P.length;i+=3)mx=Math.max(mx,Math.hypot(P[i]-prev[i],P[i+1]-prev[i+1],P[i+2]-prev[i+2]));
   D[j]=(j?D[j-1]:0)+mx+.05;prev=P;}
  return{N,D};
 }
 // the path itself at s (0..1, no easing): segment weights by length for 'arc' (palm, fingertip and finger travel)
 function pathAt(list,s,w){
  const W_=w==='arc'?list._w||(list._w=list.slice(1).map((s_,i)=>{const a=list[i].B,b=s_.B;let d=0;for(const q of ARCPTS){const p=xf(a,q),r=xf(b,q);d=Math.max(d,Math.hypot(p[0]-r[0],p[1]-r[1],p[2]-r[2]));}return Math.max(50,d,poseTravel(list[i].pose,s_.pose));})):w||list.slice(1).map(()=>1),tot=W_.reduce((a,b)=>a+b,0);let x=clamp(s)*tot,i=0;   // smoothstep (peak speed 1.5x, was 3x)
  while(i<W_.length-1&&x>W_[i]){x-=W_[i];i++;}const k=clamp(x/W_[i]),a=list[i],b=list[i+1];
  const P=j=>{const q=list[Math.max(0,Math.min(list.length-1,j))].B;return[q[12],q[13],q[14]];},d2=(p,q)=>Math.hypot(p[0]-q[0],p[1]-q[1],p[2]-q[2]);
  // Catmull-Rom tangents, clamped to the shorter neighbouring segment (no overshoot past a short end segment)
  const tan=j=>{if(j<=0||j>=list.length-1)return[0,0,0];const p=P(j-1),c=P(j),n=P(j+1),a=d2(p,c),b=d2(c,n);if(a<.5||b<.5)return[0,0,0];
   const m=[0,1,2].map(d=>(n[d]-p[d])*.5),k=Math.min(1,Math.min(a,b)/Math.max(1e-6,Math.hypot(...m)));return m.map(v=>v*k);};
  const p1=P(i),p2=P(i+1),m1=tan(i),m2=tan(i+1),k2=k*k,k3=k2*k,h00=2*k3-3*k2+1,h10=k3-2*k2+k,h01=-2*k3+3*k2,h11=k3-k2;
  const M=blendM(a.B,b.B,k);[0,1,2].forEach(d=>{M[12+d]=h00*p1[d]+h10*m1[d]+h01*p2[d]+h11*m2[d];});
  const kp=d2(p1,p2)<.5?k:clamp((k-.14)/.86);PATHDBG=[i,+k.toFixed(3)];
  return{B:M,pose:mixHandPose(a,b,kp),ap:a.ap};
 }
 // a transfer between two grips on one object (let go, move round the object, take hold again): the waypoint is
 // searched once per object size (directions round the object x distances) so the whole path stays clear of it
 // (sampled contact check, same as the penetration report); the shortest clear path wins
 const PATHS={};
 function safePath(rm,key,gA,gB,boxes,back=35,mB){
  const k=[rm.id,key,isMitt()?1:0].join('|');if(PATHS[k])return PATHS[k];
  const R=rigOf(rm),Sm=isMitt()?R.mitt:R.bare,A=st(gA),B=mB?moveS(st(gB),mB):st(gB);
  const evalP=list=>{let m=0;for(let i=0;i<=120;i++){const s=pathS(list,i/120,PW);m=Math.max(m,RH.pen(Sm,RH.fk(R,s.B,s.pose),boxes).max);if(m>3)break;}return m;};
  const Ap={...A,pose:fpre(gA,A),preOf:gA,g:null},Bp={...B,pose:fpre(gB,B),preOf:gB,g:null};let best=null;
  // when nothing clears at `back`, the hand backs off further first (25 / 50 mm more: thick cases)
  for(const bk of[back,back+25,back+50]){
   const Ab=backOff(Ap,bk),Bb=backOff(Bp,bk),mid=mixS(Ab,Bb,.5);
   // + 350, 430 mm (thick cases)
   for(const dist of[50,90,130,170,220,280,350,430])for(const d of[[0,1,0],[0,-1,0],[1,0,0],[-1,0,0],[.7,.7,0],[-.7,.7,0],[.7,-.7,0],[-.7,-.7,0],[0,.7,.7],[0,.7,-.7],[0,-.7,.7],[0,-.7,-.7]]){
    const W={B:mul(T4(d[0]*dist,d[1]*dist,d[2]*dist),mid.B),pose:openThumb(fpre(gA,A)),ap:mid.ap},list=[A,Ap,Ab,W,Bb,Bp,B],m=evalP(list);
    const sc=(m>.6?1000+m*100:0)+dist+bk;if(!best||sc<best.sc)best={sc,m,list,d,dist,back:bk};if(m<=.6)break;}
   if(best&&best.m<=.6)break;}
  return PATHS[k]=best;
 }
 const PW='arc',ARCPTS=[[0,0,0],[190,0,0],[0,190,0],[0,0,190]];
 // a segment's finger travel (mm): per digit the sum of its joints' angle changes x ~45 mm (a joint to the tip), the largest digit
 const poseTravel=(a,b)=>{if(!a||!b)return 0;let m=0;for(const d of['thumb','index','middle','ring','pinky']){let s=0;for(const k of RH.CH(d))s+=Math.abs(a[k*2]-b[k*2])+Math.abs(a[k*2+1]-b[k*2+1]);m=Math.max(m,s*45);}return m;};   // the palm and three points a fingertip's distance out (rotation counts)
 // the grip's approach pose: the fingers as they approach (spec.pre), the thumb lifted just off its surface (the lift
 // that clears it by ~3 mm with the least motion, searched once per grip)
 function fpre(g,s){if(g.fpre)return g.fpre;const R=rigOf(ROOMS[Object.keys(ROOMS)[0]]),Sm=isMitt()?R.mitt:R.bare,ks=RH.CH('thumb'),only={};ks.forEach(k=>only[k]=1);
  let best=null;
  const D0=[[-.3,-.12,-.25],[.3,0,-.25],[0,-.2,-.35],[-.15,-.1,-.4],[0,0,-.45],[.15,-.15,-.3],[-.45,0,0],[.45,0,0]];
  // Larger lifts for thicker cases (a thick case, PAL 17 mm, needs extra clearance to maintain a 3 mm gap)
  for(const d of[...D0,...D0.map(q=>q.map(v=>v*1.6)),...D0.map(q=>q.map(v=>v*2.2)),[0,0,0]]){
   const p=RH.copy(g.spec.pre);ks.forEach((k,j)=>{p[k*2]=s.pose[k*2]+d[j];p[k*2+1]=s.pose[k*2+1];});
   const gap=RH.pen(Sm,RH.fk(R,g.B,p),g.spec.boxes,only).gap,mag=Math.abs(d[0])+Math.abs(d[1])+Math.abs(d[2]),sc=(gap<=-3?0:100+gap*10)+mag;
   if(!best||sc<best.sc)best={sc,p};}
  return g.fpre=best.p;}
 // taking hold (k 0..1): the relaxed reaching hand takes the grip's approach pose with the thumb already where it
 // ends (it slides onto its surface along the approach), moves in (clear by construction: the solver approached in
 // that pose), then the fingers curl onto the object along the solver's own path (index first, the others a moment
 // later). Letting go is the same backwards.
 // the open reaching hand with the thumb already in the grip's lifted approach position (the open
 // pose's spread thumb cut through a thick case, PAL 17 mm, on the way in)
 const openThumb=pre=>{const p=RH.copy(P_OPEN());RH.CH('thumb').forEach(k=>{p[k*2]=pre[k*2];p[k*2+1]=pre[k*2+1];});return p;};
 function closeOn(g,k,back=30){const S=st(g),pre=fpre(g,S);
  if(k<.55){const a=ease(clamp(k/.55)),p=RH.stagePose(openThumb(pre),pre,smooth(clamp(k/.5))),wb=bump(k,.08,.42);   // the fingers spread a little before they close (anticipation, still >= 4 mm off)
   if(wb>0)['index','middle','ring','pinky'].forEach((d,i)=>RH.CH(d).forEach((kk,j)=>{p[kk*2]-=wb*[.2,.16,.1][j];p[kk*2+1]+=wb*.04*(i-1.5);}));
   return{B:backOff(S,back*(1-a)).B,pose:p,ap:S.ap};}
  return{B:S.B,pose:RH.curlPose(pre,g,(k-.55)/.45),ap:S.ap};}
 // a hand from a state in object space: Mo = object -> world
 const handOf=(Mo,s,o={})=>Object.assign({M:mul(Mo,s.B),pose:s.pose},o);
 function handMats(rm,h){
  if(!h)return null;
  const R=rigOf(rm),sl=h.slide||0,WD=rm.A.hand2?rm.A.hand2.slice(0,3):HAND_WRIST;
  const M=sl?mul(h.M,T4(WD[0]*sl,WD[1]*sl,WD[2]*sl)):h.M;
  let sv=M;
  if(h.ref){const R0=frameMat(h.ref),k=h.twist==null?.55:h.twist,col=j=>[M[j*4],M[j*4+1],M[j*4+2]],rcol=j=>[R0[j*4],R0[j*4+1],R0[j*4+2]];
   const ex=nrm(col(0).map((v,i)=>lerp(v,rcol(0)[i],k)));let ey=col(1).map((v,i)=>lerp(v,rcol(1)[i],k));const dd=dot(ey,ex);ey=nrm(ey.map((v,i)=>v-ex[i]*dd));
   const sg=Math.sign(dot(cross(col(0),col(1)),col(2)))||1,ez=cross(ex,ey).map(v=>v*sg),w=R.sleevePivot,W=xf(M,w);
   sv=mul(T4(...W),mul(M3(ex,ey,ez),T4(-w[0],-w[1],-w[2])));}
  return RH.fk(R,M,h.pose||RH.POSES.relax,sv);
 }
 // the contact check of a hand (mm into its object; 0 = resting on or clear of it)
 function handPen(rm,h,full){if(!h||!h.chk||(h.slide||0)>=300)return full?{max:0,bone:-1}:0;const R=rigOf(rm),r=RH.pen(isMitt()?R.mitt:R.bare,handMats(rm,h),h.chk);return full?r:r.max;}
 // both hands: one draw each (17 matrices + the mitten / bare range); the contact check runs alongside
 function handsDraw(P,rm,Rh,Lh,o={}){
  if(!Rh&&!Lh)return;const R=rigOf(rm),rg=R.ranges,mit=isMitt();
  const first=mit?rg.sleeve[0]:rg.hand[0],end=mit?rg.mitt[0]+rg.mitt[1]:rg.sleeve[0]+rg.sleeve[1];
  bindMesh(P,rm.m);gl.disable(gl.CULL_FACE);
  for(const h of[Rh,Lh]){if(!h)continue;
   draw(P,rm.T[2],first,end-first,{pal:palette(handMats(rm,h)),kind:4,skin:OPT.dbg==='skin'?[0,3,0]:SKINS[OPT.skin]||SKINS[2],glove:GLOVE_OFF[OPT.glove]||[0,0],tint:o.light||o.tint});
   if(STATS)STATS.pen=Math.max(STATS.pen||0,handPen(rm,h));}
  gl.enable(gl.CULL_FACE);
  if(STATS){STATS.hands=(Rh?1:0)+(Lh?1:0);}
 }
 // a little "weight" bump: a damped bounce that starts at t = 0
 const settle=t=>t<=0?0:Math.sin(t*15)*Math.exp(-t*7);
 // overshoot-and-settle on a 0..1 move: on target at .82, swings ~A past it, settles exactly at 1 with zero
 // speed (clips still start and end on their poses); played backwards (put back, hands leaving) it is anticipation
 const overS=(v,A=.03)=>{v=clamp(v);return smooth(v/.82)+A*Math.sin(Math.PI*clamp((v-.62)/.38))**2;};   // smoothstep (peak speed 1.8x, was 3.7x)
 const bump=(v,a,b)=>Math.sin(Math.PI*clamp((v-a)/(b-a)))**2;   // 0 -> 1 -> 0 over a..b, smooth at both ends

 // ------------------------------------------------------------------ the QR card: in its stand, or held up by the hand
 const CARD_GRAB=.34;   // .3 -> .34, a slower curl
 function cardMatrix(rm,w,cam){
  const a=rm.A.card,piv=rm.parts.a_card.pivot,u=w.share.p;if(u<.002)return I4;
  const Rs=mul(rotY(a[3]),rotX(a[4]));
  const t=w.t,zoom=w.share.zoom,size=lerp(178,214,zoom),d=cam.F*a[6]/size,tilt=.05*Math.sin(IW*t),c=Math.cos(tilt),s=Math.sin(tilt);
  const r=[0,1,2].map(i=>cam.R[i]*c+cam.U[i]*s),up=[0,1,2].map(i=>cam.U[i]*c-cam.R[i]*s);
  const off=lerp(-14,0,zoom)*d/cam.F,dy=(-6+2*Math.sin(IW*t+1.3))*d/cam.F;   // idles on the IDLE_P period (seamless loops)
  const held=[0,1,2].map(i=>cam.pos[i]+cam.Fw[i]*d+cam.R[i]*off+cam.U[i]*dy);
  const v=clamp((u-CARD_GRAB)/(1-CARD_GRAB)),k=overS(v,.03),lift=Math.sin(Math.PI*v)*60;   // the hand takes the card first, then lifts it
  const sr=[Rs[0],Rs[1],Rs[2]],su=[Rs[4],Rs[5],Rs[6]];
  const R_=nrm([0,1,2].map(i=>lerp(sr[i],r[i],k))),U_=nrm([0,1,2].map(i=>lerp(su[i],up[i],k)));
  const N_=cross(R_,U_).map(v=>-v);
  const pos=[0,1,2].map(i=>lerp(a[i],held[i],k)+(i===1?lift:0));
  return mul(T4(...pos),mul(M3(R_,U_,N_.map(v=>-v)),mul(tr(Rs),T4(-piv[0],-piv[1],-piv[2]))));
 }
 function cardCorners(rm,w,cam){const M=cardMatrix(rm,w,cam),a=rm.A.card,p=rm.parts.a_card.pivot,R=mul(rotY(a[3]),rotX(a[4]));
  const L=(x,y)=>{const q=[p[0]+R[0]*x+R[4]*y,p[1]+R[1]*x+R[5]*y,p[2]+R[2]*x+R[6]*y];return xf(M,q);};
  const w2=a[5]/2,h2=a[6]/2;return[L(-w2,h2),L(w2,h2),L(w2,-h2),L(-w2,-h2)];}

 // ------------------------------------------------------------------ world state
 const pose=(rm,id)=>{const v=rm.A['pose.'+id];return{pos:v.slice(0,3),look:v.slice(3,6),F:v[6],eye:v[7],conv:v[8]};};
 function homePose(rm,w){const b=pose(rm,'home'),a=rm.A['focus.'+w.focus];
  return{pos:[lerp(b.pos[0],a[0],.07),b.pos[1],b.pos[2]],look:[lerp(b.look[0],a[0],.16),lerp(b.look[1],a[1],.1),lerp(b.look[2],a[2],.08)],F:b.F,eye:b.eye,conv:b.conv};}
 function mixPose(a,b,u,arc=0){const k=ease(u),s=Math.sin(Math.PI*clamp(u))*arc;
  return{pos:[0,1,2].map(i=>lerp(a.pos[i],b.pos[i],k)+(i===1?s:0)),look:[0,1,2].map(i=>lerp(a.look[i],b.look[i],k)),F:1/lerp(1/a.F,1/b.F,k),eye:lerp(a.eye,b.eye,k),conv:Math.exp(lerp(Math.log(a.conv),Math.log(b.conv),k))};}
 const arcFor=p=>p==='library'?90:p==='together'?60:160;
 function libWorld(){return{level:'wall',row:0,hsel:0,hv:0,hy:0,camx:0,zoom:0,zFrom:0,zTo:0,zT0:-10,cur:0,cv:0,fsel:0,hold:0,holdT:0,speed:0,
  p:0,pFrom:0,pTo:0,pT0:-10,pDur:1,flip:0,flipFrom:0,flipTo:0,flipSide:0,flipT0:-10,arm:false,lastMove:0,auto:true,obv:0,obEnter:0,lcam:0,letterT:-10,favs:new Set(R8.LIB.fav.map(g=>g.sys+'/'+g.title))};}
 function world(rm,focus='continue'){
  const w={t:0,mode:'home',focus,focusT:-5,place:null,t0:-10,from:null,att:null,attV:null,L:libWorld(),tune:0,tuneSel:0,tuneV:0,scope:{prev:0,cur:0,t0:-5},
   share:{p:0,from:0,to:0,t0:-10,dur:1,zoom:0,zTo:0},pick:{on:false,k:0,row:0,t0:-10},tog:{sel:0,lift:[0,0]},playT0:-10,play:0,clock:[21,47],after:null,open:null,rm,fade:null};
  library(rm);{const i=Math.max(0,LIBROWS.findIndex(r=>r.sid==='ps1'));w.L.row=w.L.hsel=i;w.L.hy=LIBROWS[i].base;w.L.camx=LIBROWS[i].cx;}const h=homePose(rm,w);w.att={pos:h.pos.slice(),look:h.look.slice()};w.attV={pos:[0,0,0],look:[0,0,0]};return w;
 }
 // ---- the library camera: the wall pose, gliding into one row (head-on), pulled back when scrolling fast
 function slotLX(row,f){const s=row.games,i=clamp(Math.floor(f),0,s.length-1),j=Math.min(s.length-1,i+1);return lerp(s[i].lx,s[j].lx,clamp(f-i,-.5,1.5));}
 // At native device resolution: closer to the shelf so the titles on the spines are readable at 400x240
 const SHELF_D=360;
 function shelfPose(rm,w,pkTo){
  const L=w.L,row=LIBROWS[L.row],hm=row.hmax||(row.hmax=Math.max(...row.games.map(g=>g.b.H))),D0=lerp(Math.max(325,540*(hm+26)/240),560,smooth(L.speed))   /* the device's close-up (room_section_pose, 10 % nearer) */,pk=pkTo!=null?pkTo:smooth((L.p-DOLLY[0])/(DOLLY[1]-DOLLY[0])),D=lerp(D0,660,pk),x=L.camx,y=row.base+hm/2+4+20*pk;   // a box in the hand: step back to look at it (over the first part of the take-out)
  return{pos:[x,y+8,-D],look:[x,y,0],F:600,eye:lerp(12,18,L.speed),conv:D};
 }
 // the wall view: four rows of the case at a time, following the highlighted section's row
 function wallPose(rm,w){const C=rm.CASE,L=w.L,top=C.rows*C.gap,owned=LIBROWS.length?LIBROWS[LIBROWS.length-1].base:0;
  const lo=Math.min(owned+2*C.gap,top-2*C.gap),y=clamp(L.hy+C.gap/2,Math.max(2*C.gap,lo),top-2*C.gap+20),D=1030,x=C.x0+C.bay*C.nbay/2;
  return{pos:[x,y+40,-D],look:[x,y,0],F:300,eye:40,conv:D};}
 function libPose(rm,w){const L=w.L,wl=wallPose(rm,w);if(L.zoom<=.0005)return wl;return mixPose(wl,shelfPose(rm,w),L.zoom,0);}
 function curPose(w){const rm=w.rm;
  if(w.mode==='home')return{pos:w.att.pos.slice(),look:w.att.look.slice(),F:pose(rm,'home').F,eye:pose(rm,'home').eye,conv:pose(rm,'home').conv};
  if(w.mode==='go')return mixPose(w.from,w.place==='library'?libPose(rm,w):pose(rm,w.place),clamp((w.t-w.t0)/GLIDE),arcFor(w.place));
  if(w.mode==='back')return mixPose(w.from,homePose(rm,w),clamp((w.t-w.t0)/GLIDE),arcFor(w.place));
  if(w.mode==='lib')return libPose(rm,w);
  let p=pose(rm,w.place);
  if(w.place==='settings'&&OPT.setShift){const[dx,dz,dy]=OPT.setShift;p=Object.assign({},p,{pos:[p.pos[0]+dx,p.pos[1]+(dy||0),p.pos[2]+dz],look:[p.look[0]+dx,p.look[1]+(dy||0),p.look[2]]});}   // settings-radio stills: room for the help tag
  if(w.place==='continue'&&w.play>0){const k=ease(clamp(w.play/.6)),s=rm.A.screen;p=Object.assign({},p,{pos:[lerp(p.pos[0],s[0],k),lerp(p.pos[1],s[1],k),lerp(p.pos[2],s[2]-300,k)]});}
  return p;}
 function go(w,place){sfx(w,'glide',{to:place});w.from=curPose(w);w.place=place;w.mode='go';w.t0=w.t;w.share.zoom=w.share.zTo=0;if(place==='together')w.tog.sel=0;
  if(place==='library'){const L=w.L;L.level='wall';L.zoom=L.zFrom=L.zTo=0;}}
 // The way back starts from the current camera pose; reading it after switching mode causes a cut.
 function back(w){sfx(w,'back');sfx(w,'glide',{back:true,from:w.place});if(w.place==='settings')sfx(w,'settings-leave');const from=curPose(w);w.mode='back';w.t0=w.t;w.from=from;}
 function shareTo(w,to,dur){const s=w.share;s.from=s.p;s.to=to;s.t0=w.t;s.dur=dur;}
 function pull(L,to,dur,t){if(to===1){L.flip=L.flipFrom=L.flipTo=L.flipSide=0;}L.pFrom=L.p;L.pTo=to;L.pT0=t;L.pDur=dur;}
 function enterRow(w,r){sfx(w,'enter-row',{section:r,sys:LIBROWS[r].sid});const L=w.L,row=LIBROWS[r];L.row=r;L.level='shelf';L.cur=L.fsel=row.sel;L.cv=0;L.zFrom=L.zoom;L.zTo=1;L.zT0=w.t;L.arm=true;L.lastMove=w.t+1.1;
  L.lcam=slotLX(row,L.cur);L.camx=clamp(boxX(row,row.games[row.sel],secOff(row,L.lcam)),row.cx-58,row.cx+58);}
 // wall navigation: left / right within a row, up / down between rows (same column, or the row's last)
 function wallMove(w,k){const L=w.L,n=LIBROWS.length,i=L.hsel,s=LIBROWS[i],nb=w.rm.CASE.nbay;let j=i;
  if(k==='LEFT'&&s.c>0)j=i-1;if(k==='RIGHT'&&s.c<nb-1&&i+1<n)j=i+1;
  if(k==='UP'&&i-nb>=0)j=i-nb;if(k==='DOWN'&&i+nb-s.c<n)j=Math.min(n-1,i+nb);
  if(j!==i){L.hsel=j;sfx(w,LIBROWS[j].r!==s.r?'row':'section',{section:j,sys:LIBROWS[j].sid});}}
 function libAct(w,k){
  const L=w.L;
  if(w.open){if(!w.open.done&&(k==='A'||k==='B'))w.open.skip=w.t;return;}
  if(L.level==='wall'){
   if(Math.abs(L.zoom-L.zTo)>.01)return;
   if(k==='UP'||k==='DOWN'||k==='LEFT'||k==='RIGHT'){wallMove(w,k);return;}
   if(k==='A'){enterRow(w,L.hsel);return;}
   if(k==='B'){back(w);return;}
   return;}
  const row=LIBROWS[L.row],n=row.games.length;
  if((k==='L'||k==='R')&&L.pTo===1&&L.p>.98){L.flipSide=(L.flipSide+(k==='R'?1:2))%3;sfx(w,'flip',{side:['front','spine','back'][L.flipSide]});L.flipFrom=L.flip;L.flipTo=L.flipSide*Math.PI/2;   /* + turns the spine (-z) to the camera (- brought the free edge) */L.flipT0=w.t;return;}
  if(k==='HOLDR'||k==='HOLDL'){if(L.pTo===1)pull(L,0,.4,w.t);L.hold=k==='HOLDR'?1:-1;L.holdT=w.t;L.fsel=row.sel;L.arm=true;return;}
  if(k==='REL'){L.hold=0;return;}
  if(k==='LEFT'||k==='RIGHT'||k==='L'||k==='R'){
   let i=row.sel;const d=k==='LEFT'?-1:k==='RIGHT'?1:0;
   if(d)i=clamp(i+d,0,n-1);
   else{// L/R: jump to the previous/next first letter
    const cur=row.games[i].letter;if(k==='R'){while(i<n-1&&row.games[i].letter===cur)i++;}else{while(i>0&&row.games[i-1].letter===cur)i--;if(i===row.sel&&i>0){const p=row.games[i-1].letter;i--;while(i>0&&row.games[i-1].letter===p)i--;}}L.letterT=w.t;sfx(w,'letter',{letter:row.games[i].letter});}
   if(L.pTo===1)pull(L,0,.4,w.t);
   if(i===row.sel){L.cv+=Math.sign(d||1)*.8;return;}
   row.sel=i;L.fsel=i;L.lastMove=w.t;L.arm=true;return;}
  if(k==='A'){
   if(L.pTo===0&&L.p<.02){pull(L,1,PULL_T,w.t);L.arm=false;caseTexture(row.games[row.sel]);}
   else if(L.pTo===1&&L.p>.98){const g=row.games[row.sel];w.open={t0:w.t,g,skip:null,done:false};sfx(w,'play',{sys:SYS[g.sys].prop});}
   return;}
  if(k==='B'){
   if(L.pTo===1){pull(L,0,PUT_T,w.t);L.arm=false;return;}
   if(L.p<.02){L.level='wall';L.hsel=L.row;L.zFrom=L.zoom;L.zTo=0;L.zT0=w.t;sfx(w,'leave-row',{section:L.row});}
   return;}
  if(k==='Y'){const g=row.games[row.sel],key=g.sys+'/'+g.title;if(L.favs.has(key))L.favs.delete(key);else L.favs.add(key);sfx(w,'fav',{on:L.favs.has(key)});}
 }
 function libStep(w,dt){
  const L=w.L,rm=w.rm,C=rm.CASE,row=LIBROWS[L.row],n=row.games.length;
  L.zoom=lerp(L.zFrom,L.zTo,ease((w.t-L.zT0)/1.15));
  L.flipBase=lerp(L.flipFrom,L.flipTo,ease((w.t-L.flipT0)/TURN_T));
  L.flip=lerp(L.flipFrom,L.flipTo,overS((w.t-L.flipT0)/TURN_T,.06));   // box and wrist turn a little past and settle (follow-through)
  // the take-out clock runs linearly; every phase on it has its own ease (an eased clock under eased
  // phases multiplied the speeds: the regrip took ~5 frames)
  L.p=lerp(L.pFrom,L.pTo,clamp((w.t-L.pT0)/L.pDur));
  L.flipLin=lerp(L.flipFrom,L.flipTo,clamp((w.t-L.flipT0)/TURN_T));
  const hacc=-60*(L.hy-LIBROWS[L.hsel].base)-14*L.hv;L.hv+=hacc*dt;L.hy+=L.hv*dt;
  // holding left/right: scroll continuously, faster and faster (the camera pulls back)
  if(L.hold&&L.level==='shelf'){const held=w.t-L.holdT;if(held>.3){const v=Math.min(46,7+34*(held-.3));L.fsel=clamp(L.fsel+L.hold*v*dt,0,n-1);const i=Math.round(L.fsel);if(i!==row.sel){row.sel=i;L.lastMove=w.t;}}
   else if(held<.02&&row.sel===Math.round(L.fsel)){const i=clamp(row.sel+L.hold,0,n-1);row.sel=i;L.fsel=i;L.lastMove=w.t;}}
  const prev=L.cur,acc=-150*(L.cur-row.sel)-16*L.cv;L.cv+=acc*dt;L.cur+=L.cv*dt;
  if(L.level==='shelf'&&Math.round(L.cur)!==Math.round(prev))sfx(w,'scroll-tick',{speed:+Math.abs(L.cv).toFixed(1)});
  const sp=clamp(Math.abs(L.cur-prev)/dt/30);L.speed+=(sp-L.speed)*Math.min(1,dt*(sp>L.speed?3:4));
  if(L.speed>.25)L.letterT=w.t;
  // the camera's logical position and window: the camera never leaves the middle bay; boxes,
  // uprights and planks repeat every bay, so moving the window by one bay is invisible
  // inside a section the camera stays near its middle; the boxes slide along it and disappear behind the uprights
  if(L.zoom>.001||L.level==='shelf'){L.lcam=slotLX(row,L.cur);const tx=clamp(boxX(row,row.games[Math.round(clamp(L.cur,0,n-1))],secOff(row,L.lcam)),row.cx-58,row.cx+58);L.camx+=(tx-L.camx)*Math.min(1,dt*7);}
  // Browsing: no auto take-out on hover: browsing only tips the box out (TIP), A takes it
 }
 function act(w,k){
  if(w.t-w.playT0<1.6)return;
  if(w.mode==='home'){if(NAV[w.focus][k]){w.focus=NAV[w.focus][k];w.focusT=w.t;sfx(w,'focus',{place:w.focus});return;}if(k==='A')go(w,w.focus);if(k==='SELECT'||k==='X'){w.focus='settings';go(w,'settings');}return;}
  if(w.mode==='lib'){libAct(w,k);return;}
  if(w.mode!=='at')return;const p=w.place;
  if(p==='settings'&&w.pick.on){pickAct(w,k);return;}
  if(k==='B'){if(p==='share'&&w.share.to===1){shareTo(w,0,CARD_OUT);w.after={t:w.t+CARD_OUT-.05,f:'back'};return;}back(w);return;}
  if(p==='settings'){const d=k==='UP'||k==='LEFT'?-1:k==='DOWN'||k==='RIGHT'?1:0;if(d){const s=clamp(w.tuneSel+d,0,NCH-1);if(s!==w.tuneSel){w.tuneSel=s;sfx(w,'knob',{ch:s});const sc=w.scope;sc.prev=clamp((w.t-sc.t0)/.35)<1?sc.prev:sc.cur;sc.cur=s;sc.t0=w.t;}return;}
   if(k==='A'&&w.tuneSel===ROOM_CH){w.styleReq=1;sfx(w,'style');return;}
   if(k==='A'&&w.tuneSel===HAND_CH){w.pick.on=true;w.pick.t0=w.t;w.pick.row=0;sfx(w,'hand-in',{picker:true});return;}}
  if(p==='together'&&(k==='LEFT'||k==='RIGHT')){w.tog.sel=k==='LEFT'?0:1;return;}
  if(p==='share'&&k==='A'){if(w.share.to===0)shareTo(w,1,CARD_IN);else{w.share.zTo=w.share.zTo?0:1;sfx(w,'card-zoom',{zoom:w.share.zTo});}return;}
  if(p==='continue'&&k==='A'){w.playT0=w.t;return;}
 }
 // the hand picker (CH6 HAND, A): both hands rise in front of the scope; up/down = row, left/right = value
 function pickAct(w,k){const P_=w.pick;
  if(k==='B'||k==='A'){P_.on=false;P_.t0=w.t;sfx(w,'hand-out',{picker:true});return;}
  if(k==='UP'||k==='DOWN'){P_.row=k==='UP'?0:1;sfx(w,'tick');return;}
  const d=k==='LEFT'?-1:k==='RIGHT'?1:0;if(!d)return;
  if(P_.row===0){OPT.skin=String((+OPT.skin-1+d+4)%4+1);sfx(w,'skin',{skin:+OPT.skin});}
  else{OPT.glove=(OPT.glove+d+GLOVES.length)%GLOVES.length;sfx(w,'glove',{glove:GLOVES[OPT.glove]});}}
 const BLACK_T=.55,BACK_T=.45;
 function step(w,dt){
  w.t+=dt;const rm=w.rm;
  if(w.mode==='home'){const h=homePose(rm,w);for(const k of['pos','look'])for(let i=0;i<3;i++){const v=w.attV[k];v[i]+=(-34*(w.att[k][i]-h[k][i])-11*v[i])*dt;w.att[k][i]+=v[i]*dt;}}
  if(w.mode==='go'&&w.t-w.t0>=GLIDE){sfx(w,'arrive',{place:w.place});if(w.place==='library'){w.mode='lib';w.t0=w.t;}else{w.mode='at';w.t0=w.t;if(w.place==='share')w.after={t:w.t+.25,f:'lift'};if(w.place==='settings')sfx(w,'settings-enter');}}
  if(w.mode==='back'&&w.t-w.t0>=GLIDE){w.mode='home';const h=homePose(rm,w);w.att={pos:h.pos.slice(),look:h.look.slice()};w.attV={pos:[0,0,0],look:[0,0,0]};w.place=null;}
  libStep(w,dt);
  if(w.after&&w.t>=w.after.t){const f=w.after.f;w.after=null;if(f==='lift'&&w.mode==='at')shareTo(w,1,CARD_IN);if(f==='back')back(w);}
  const s=w.share;s.p=lerp(s.from,s.to,clamp((w.t-s.t0)/s.dur));   // linear clock, eased phasess.zoom+=(s.zTo-s.zoom)*Math.min(1,dt*6);
  w.tuneV+=(-90*(w.tune-w.tuneSel)-15*w.tuneV)*dt;w.tune+=w.tuneV*dt;
  for(let i=0;i<2;i++)w.tog.lift[i]+=((w.mode==='at'&&w.place==='together'&&w.tog.sel===i?1:0)-w.tog.lift[i])*Math.min(1,dt*7);
  w.play=w.playT0>0?clamp((w.t-w.playT0)/1.4):0;if(w.play>=1)w.playT0=-10;
  w.pick.k+=((w.pick.on&&w.mode==='at'&&w.place==='settings'?1:0)-w.pick.k)*Math.min(1,dt*4.5);
  w.pick.s=clamp((w.pick.s||0)+(w.pick.on&&w.mode==='at'&&w.place==='settings'?1:-1)*dt/PICK_T);   // the hands' own linear clock
  if(w.mode!=='at')w.pick.on=false;
  events(w);
  const o=w.open;if(o){const e=openT(w);if(!o.done&&e>=openLen(o)+BLACK_T){o.done=true;o.t1=w.t;if(w.onStart)w.onStart(o.g);}if(o.done&&w.t-o.t1>.9+BACK_T)w.open=null;}
 }
 function events(w){const L=w.L;
  // the hand rises, the case slides out (+ lifts off), the hand takes it; backwards: it slides home, the hand leaves
  evTrack(w,'pull',L.p,[[REACH[0]+.03,'hand-in',1],[FLY[0]+.02,'slide-out',1],[FLY[0]+.02,'pick',1],[CLOSE[0]+.14,'catch',1],[FLY[0]+.14,'slide-in',-1],[REACH[0]+.1,'hand-out',-1]]);
  const o=w.open,ps=o&&SYS[o.g.sys].prop==='ps1';
  evTrack(w,'open',o&&!o.done?openE(openT(w)):-1,!o?[]:ps?[[.26,'hand-in',1],[.36,'catch',1],[.5,'open-ps1',1],[.6,'start',1,{sys:'ps1'}]]:
   [[.26,'hand-in',1],[.36,'catch',1],[.43,'open-gba',1],[.56,'cart-pop',1],[.58,'start',1,{sys:'gba'}]]);
  evTrack(w,'share',w.share.p,[[.2,'hand-in',1],[CARD_GRAB,'catch',1],[CARD_GRAB,'card-pick',1],[CARD_GRAB,'card-down',-1],[.12,'hand-out',-1]]);
  evTrack(w,'chan',w.t-w.scope.t0,[[.35,'channel',1,{ch:w.scope.cur}]]);
 }
 const openT=w=>{const o=w.open;return o.skip!=null?Math.max(w.t-o.t0,openLen(o)+(w.t-o.skip)):w.t-o.t0;};

 // ------------------------------------------------------------------ the pulled-out box
 // box-local (props) frame: x = thickness (front at +x), y = 0..H, z = width (spine at -z).
 // On the shelf the spine faces the room (-z): identity. Held: the front faces the camera.
 function boxBasis(th){const c=Math.cos(th),s=Math.sin(th);return[[c,0,s],[0,1,0],[-s,0,c]];}
 // Smooth motion: every hand move <= 30 mm per 60 Hz frame (tools/breadbox-r10-motion.py)
 // Case motion: the take-out and the put-back are 1.1 s, the
 // case flies (see pulledBox); other animations keep independent timing
 const PULL_T=1.1,PUT_T=1.1,PICK_T=.7,TURN_T=1.1,OPEN_K=1.2,OPEN_SLOW=[.1,.5,2.2],CARD_IN=2.1,CARD_OUT=2.1;
 // the opening's clock: seconds -> e (the pose timeline in openPose), OPEN_K slower overall and the hand's move to
 // the lid / flap (e .24-.5) OPEN_SLOW[2] slower again; openSec is its inverse
 function openE(t){const[a,b,f]=OPEN_SLOW,e=t/OPEN_K;return e<=a?e:e<=a+(b-a)*f?a+(e-a)/f:e-(b-a)*(f-1);}
 function openSec(e){const[a,b,f]=OPEN_SLOW;return(e<=a?e:e<=b?a+(e-a)*f:e+(b-a)*(f-1))*OPEN_K;}
 // Two-hand take-out timeline (u = L.p, 0 = the box tipped
 // TIP mm out of its slot by the browsing code, 1 = held up; put back plays it backwards and ends tipped):
 //   left hand reaches in (0-.2) and closes on the tipped box (.11-.29), pulls it straight out (.3-.48), lifts it to
 //   the view and turns it to face the camera (.44-.7); the right hand comes in (.55-.7) and takes the free edge
 //   (.62-.82), the left lets go (.78-.97) and leaves (.9-1); the box settles into the hold. Sideways it only
 //   travels with the lift (one eased arc to the view's middle); nothing drifts while it is held. The hold idle repeats every IDLE_P seconds (any IDLE_P window loops seamlessly).
 const TIP=18,IDLE_P=4,IW=2*Math.PI/IDLE_P;
 let HOLD_DX=0,HOLD_DY=-9;   // the held case's screen centre at x = 200, y ~ 118 (it sat 13 px high)
 // timeline (u = L.p over PULL_T = 1.1 s; no left hand, nothing pulls the case): browsing tips the case TIP mm out of
 // its cubby; on A the case slides out along its own depth (the first half of the flight), then arcs up and over to the
 // hold in front of the camera and turns to face it (the turn only starts once it is clear of the cubby), one ease-out
 // (flyS: soft start, fast middle, soft landing, no overshoot). HAND_MODE 'rise': the right hand comes into the frame
 // while the case flies and closes on it as it arrives; 'wait': it is already open at the hold point when the case leaves.
 // The put-back plays the same timeline backwards (the hand lets go and leaves, the case flies home).
 const HAND_MODE=new URLSearchParams(location.hash.slice(1)).get('bbhand')==='wait'?'wait':'rise';
 const TL=HAND_MODE==='wait'?{fly:[.30,.66],reach:[.02,.34],close:[.50,.84],dolly:[0,.30]}:{fly:[.10,.52],reach:[.06,.40],close:[.38,.72],dolly:[.04,.40]};
 const FLY=TL.fly,REACH=TL.reach,CLOSE=TL.close,DOLLY=TL.dolly;
 const HAND_BACK=+(new URLSearchParams(location.hash.slice(1)).get('bbback')||40);   // how far off the case the open hand waits (mm; the flying case turns into the space beside it)
 const FLY_ARC=26;   // the arc's height (mm)
 const flyS=v=>smooth(Math.pow(clamp(v),.75));
 function pulledBox(rm,w,cam,noFlip=false,landed=false){
  const L=w.L,C=rm.CASE,row=LIBROWS[L.row],g=row.games[row.sel],b=g.b,u=L.p,t=w.t;
  const sx=boxX(row,g,secOff(row,L.lcam)),S={x:sx,y:row.base,z:4+b.W/2-TIP};
  const s=landed?1:flyS((u-FLY[0])/(FLY[1]-FLY[0])),wi=smooth((u-CLOSE[1])/(1-CLOSE[1]));
  // Case centring: held in the middle of the view (was 30 mm left); centring checked in px by
  // tools/breadbox-r10-framing.py at the device's clip camera
  // The case flies to where the hold will be once the camera has stepped back (the camera's dolly ends before the landing):
  // the live camera plus what is left of the dolly, so the target never moves under the case
  const d=600*b.H/172,sp0=shelfPose(rm,w),sp1=shelfPose(rm,w,1),
   hold={x:cam.pos[0]+HOLD_DX+sp1.pos[0]-sp0.pos[0],y:cam.pos[1]+2-b.H/2+HOLD_DY+sp1.pos[1]-sp0.pos[1],z:cam.pos[2]+d+sp1.pos[2]-sp0.pos[2]};
  // out of the cubby straight along its depth (the neighbours and the board above stay clear: no sideways move, no
  // lift until the case is out before turning), then the arc and the turn to the camera: the first part of the flight
  // is depth only, c = the share of the way that frees the cubby
  const c=clamp((b.W+18-TIP)/Math.max(1,S.z-hold.z),.15,.85),sl=smooth((s-c)/(1-c)),kt=sl;
    // the hold's slight yaw (.16) fades out as the box turns to its spine (square to the camera) and back in on the back
  const fl=noFlip?0:L.flip*kt,yaw=.16*Math.cos(fl);
  let x=lerp(S.x,hold.x,sl),y=lerp(S.y,hold.y,sl)+FLY_ARC*bump(sl,0,1),z=lerp(S.z,hold.z,s),th=lerp(0,-Math.PI/2+yaw,kt)+fl;
  // idle (periodic, IDLE_P): a slow bob, a little yaw and pitch
  y+=wi*(2*Math.sin(IW*t));th+=wi*.03*Math.abs(Math.cos(fl))*Math.sin(IW*t+1.1);
  // the weight settles as the hand takes it (a bump: zero speed at both ends)
  y-=1.5*bump(u,FLY[1],CLOSE[1]);
  const[ex,ey,ez]=boxBasis(th),M=M3(ex,ey,ez);M[12]=x;M[13]=y;M[14]=z;
  const pm=about([0,b.H/2,0],rotZ(-.04*sl+.04*bump(sl,0,1)+wi*.012*Math.sin(2*IW*t+.4)));   // a little roll in flight
  return{M:mul(M,pm),g,b,u};
 }
 // the selected box's surroundings in the shelf, in its own space (box standing in its slot): the neighbours
 // (their own heights and depths) and the boards above and below; S0 = that space -> world
 function shelfAround(rm,row,g,L,tip=0){
  // tip: the box stands tip mm out of its slot: the neighbours and boards sit tip mm further back
  const C=rm.CASE,b=g.b,lo=[],hi=[];const ex=[];
  for(const n of row.games){if(n===g)continue;const dx=n.lx-g.lx;if(Math.abs(dx)>170)continue;
   ex.push([[dx-n.b.T/2,0,-b.W/2+tip],[dx+n.b.T/2,n.b.H,-b.W/2+n.b.W+tip]]);}
  ex.push([[-400,C.gap-C.board,-b.W/2-5+tip],[400,C.gap+20,C.depth+tip]]);       // the board above
  ex.push([[-400,-C.board-20,-b.W/2-5+tip],[400,0,C.depth+tip]]);                // the board it stands on
  const S0=T4(boxX(row,g,secOff(row,L.lcam)),row.base,4+b.W/2-tip);
  return{local:ex,S0};
 }
 const ROTY_PI=[-1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1];
 const boxIn=(b,M)=>RH.box([-b.T/2,0,-b.W/2],[b.T/2,b.H,b.W/2],M);
 const lift=(s,d)=>({B:mul(T4(0,d,0),s.B),pose:s.pose,ap:s.ap});
 const P_OPEN=()=>RH.POSES.open;
 // the right hand on the pulled box (not opening). timeline (u = L.p): the open hand reaches in over
 // the shelf (0-.2) and closes on the box (.13-.28): index and middle fingers over its top, the thumb down the
 // front of its spine; it pulls the box out (.26-.5); while the box lifts and turns to the view the hand lets go,
 // goes up and over it and closes on its free edge (.5-.78: the edge pinch, fingers round the back, thumb on
 // the front). Turning the box to its back moves the hand under it to the spine edge.
 // the right hand: it comes in as the box turns to the view and takes its free edge (the view grip), then
 // holds it (breathing on the idle period); turning the box to its back: under it to the spine edge
 const MX=[-1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
 const mirrorX=s=>({B:mul(MX,s.B),pose:s.pose,ap:[-s.ap[0],s.ap[1],s.ap[2]]});
 // the hand is tied to where the case will be held (PBH, the landed pose), not to the flying case: it rises into
 // the frame and waits open at the hold point (REACH), and closes (CLOSE) as the case lands; from the landing on the two
 // poses are the same
 function holdHand(rm,w,PB,cam){
  const L=w.L,b=PB.b,u=PB.u,t=w.t;
  if(u<REACH[0])return null;
  const PBH=pulledBox(rm,w,cam,false,true);
  const vw=grip(rm,'view',b,{gy:.46}),V=st(vw),reach=smooth((u-REACH[0])/(REACH[1]-REACH[0])),close=smooth((u-CLOSE[0])/(CLOSE[1]-CLOSE[0])),wi=smooth((u-CLOSE[1])/(1-CLOSE[1]));
  let s=close<1?closeOn(vw,close,HAND_BACK):{...V};
  const fk=u>=.98?clamp((Math.abs(L.flipLin??L.flip)-Math.PI/2)/(Math.PI/2)):0;
  if(fk>0)s=pathS(safePath(rm,'flip|'+[b.T,b.H,b.W],vw,vw,[boxIn(b)],35,ROTY_PI).list,fk,PW);
  if(close>=1&&(fk<=0||fk>=1))s={...s,pose:RH.breathe(s.pose,t,wi,IDLE_P)};
  let ref=null;if(Math.abs(L.flip)>.002&&u>.98&&fk<.01){const M=mul(pulledBox(rm,w,cam,true).M,vw.B);ref={o:[M[12],M[13],M[14]],ex:[M[0],M[1],M[2]],ey:[M[4],M[5],M[6]],ez:[M[8],M[9],M[10]]};}
  return handOf(PBH.M,s,{slide:(1-reach)*320,ref,twist:.85,chk:[boxIn(b,PB.M)]});
 }
 // The case flies to the right hand without a left-hand take-out.
 const LIDSWING=.3;   // the lid swings on a little after the hand lets go (it stops short of the left thumb)
 const OPEN={ps1:{len:openSec(1.1),fade:openSec(.6)},gba:{len:openSec(1.05),fade:openSec(.58)}};   // seconds (the opening's own clock, openE)   // the fade starts as the case opens (no disc / cartridge pull)
 const openLen=o=>OPEN[SYS[o.g.sys].prop].len;
 const mirrorS=s=>({B:mul(MZ,s.B),pose:s.pose,ap:[s.ap[0],s.ap[1],-s.ap[2]]});
 // game start: the left hand comes in and takes the case by its hinge edge (the edge pinch, mirrored);
 // the right hand lets go of the free edge and pinches the lid (PS1: thumb on the front, index curled round its
 // edge) and swings it open, then lets go; the picture fades to black as it opens. No disc is shown.
 // GBA: the right hand moves up the edge, the thumb flicks the flap open, the cartridge peeks up, the hand goes
 // back down the side of the box (it never crosses the box).
 function openPose(rm,w,PB,ref,e){
  const pk=SYS[PB.g.sys].prop==='ps1',b=PB.b,PR=props(),pp=PR.parts,k=ease(clamp((e-.25)/.6)),t=w.t;
  const Sc=[b.T/ref.T,b.H/ref.H,b.W/ref.W],dip=-5*settle(e-.36)-(pk?0:4*settle(e-.8));
  const Mb=mul(T4((pk?34:-10)*k,(pk?6:-62)*k+dip,(pk?28:80)*k),mul(PB.M,mul(about([0,b.H/2,0],rotY(-(pk?.18:.12)*k)),pk?I4:rotZ(.25*k))));
  const MbS=mul(Mb,[Sc[0],0,0,0,0,Sc[1],0,0,0,0,Sc[2],0,0,0,0,1]);
  const r={MbS,lidA:0,flapA:0,cart:I4};
  const lg=grip(rm,'view',b,{gy:.3,psi:.66,hz:-32,hy:36}),LS=mirrorS(st(lg)),lclose=smooth((e-.12)/.22);   // its own grip: not the right hand mirrored
  const Ls=mirrorS(closeOn(lg,lclose));if(lclose>=1)Ls.pose=RH.breathe(Ls.pose,t,.5,IDLE_P);
  // PS1: the left thumb rests on the lid by the hinge; it lifts off as the lid starts to open
  if(pk&&e>.42){const lf=smooth((e-.42)/.12),fp=RH.pose({thumb:[-.3,-.2,-.1,.35]});RH.CH('thumb').forEach(kk=>{Ls.pose[kk*2]+=(fp[kk*2]-Ls.pose[kk*2])*lf;Ls.pose[kk*2+1]+=(fp[kk*2+1]-Ls.pose[kk*2+1])*lf;});}
  const vg=grip(rm,'view',b,{gy:.46}),V=st(vg);
  if(pk){
   const lt=3.2*b.T/14,piv=pp.ps1_lid.pivot.map((v,i)=>v*Sc[i]),lidAt=a=>about(piv,rotY(a));
   const lidA=e=>e<.5?0:e<.58?.1*smooth((e-.5)/.08):e<.86?.1+1.05*smooth((e-.58)/.28):1.15+LIDSWING*(1-Math.pow(1-clamp((e-.86)/.35),3))-.06*settle(e-1.21);   // +.06 (the hand's longer move to the lid)
   r.lidA=lidA(e);
   // the pinch is solved with a keep-out behind the tray: no finger wraps behind it (the lid takes the hand with it)
   const lp=grip(rm,'lid',b,{extra:[RH.box([-b.T/2-90,-30,-b.W/2],[b.T/2-lt,b.H+30,b.W/2+10])]}),LP=st(lp);
   let s;
   if(e<.24)s={...V,pose:RH.breathe(V.pose,t,1,IDLE_P)};
   else if(e<.5)s=pathS(safePath(rm,'lid|'+[b.T,b.H,b.W],vg,lp,[boxIn(b)],22).list,(e-.24)/.26,PW);
   else if(e<.86)s=moveS(LP,lidAt(r.lidA));
   else{const A=moveS(LP,lidAt(lidA(.86)));s=mixS(A,backOff(A,45,P_OPEN()),smooth((e-.86)/.3));}
   const chk=[RH.box([-b.T/2,0,-b.W/2],[b.T/2-lt,b.H,b.W/2],Mb),RH.box([b.T/2-lt,0,-b.W/2],[b.T/2,b.H,b.W/2],mul(Mb,lidAt(r.lidA)))];
   r.R=handOf(Mb,s,{slide:e>.96?smooth((e-.96)/.4)*330:0,chk});
   r.L=handOf(Mb,backOff(Ls,(isMitt()?14:11)*smooth((e-.42)/.24)),{slide:(1-smooth(e/.27))*320,chk});   // the left hand eases off a little so the opening lid clears its thumb (more for the fat mitten thumb)
  }else{
   r.flapA=2.4*smooth((e-.43)/.24);
   r.cart=T4(0,18*outBack(clamp((e-.56)/.14)),0);
   const tg=grip(rm,'view',b,{gy:.84}),TG=st(tg);
   let s;
   if(e<.24)s={...V,pose:RH.breathe(V.pose,t,1,IDLE_P)};
   else if(e<.43)s=pathS(safePath(rm,'flapU|'+[b.T,b.H,b.W],vg,tg,[boxIn(b)],22).list,(e-.24)/.19,PW);
   else if(e<.62){s={...TG,pose:RH.copy(TG.pose)};const f=Math.sin(Math.PI*clamp((e-.43)/.17));RH.CH('thumb').forEach((kk,j)=>{s.pose[kk*2]-=[.9,.5,.4][j]*f;});}
   else s=pathS(safePath(rm,'flapD|'+[b.T,b.H,b.W],tg,vg,[boxIn(b)],22).list,(e-.62)/.42,PW);
   const chk=[boxIn(b,Mb)];
   r.R=handOf(Mb,s,{ref:local(Mb,gripR(b,.46)),twist:.6,chk});
   r.L=handOf(Mb,Ls,{slide:(1-smooth(e/.27))*320,chk});
  }
  return r;
 }
 // a hinged part (lid, flap) turns in the box's real proportions: the props are scaled (Sc) to the box
 // after it, so the rotation is conjugated by the scale (rotating in the props' own space sheared a lid that is not
 // 14 mm thick, and the drawn lid no longer matched the contact model)
 const hingeAt=(Sc,piv,R)=>mul([1/Sc[0],0,0,0,0,1/Sc[1],0,0,0,0,1/Sc[2],0,0,0,0,1],mul(about(piv.map((v,i)=>v*Sc[i]),R),[Sc[0],0,0,0,0,Sc[1],0,0,0,0,Sc[2],0,0,0,0,1]));
 function drawPulled(P,rm,w,cam){
  const L=w.L,PB=pulledBox(rm,w,cam),g=PB.g,sys=SYS[g.sys].prop,PR=props(),pp=PR.parts;
  caseTexture(g);
  const ref=sys==='ps1'?{T:14,H:125,W:142}:{T:27,H:137,W:120},Sc=[PB.b.T/ref.T,PB.b.H/ref.H,PB.b.W/ref.W];
  let MbS=mul(PB.M,[Sc[0],0,0,0,0,Sc[1],0,0,0,0,Sc[2],0,0,0,0,1]),R=null,Lh=null,op=null;
  if(w.open){op=openPose(rm,w,PB,ref,openE(openT(w)));MbS=op.MbS;R=op.R;Lh=op.L;}else{R=holdHand(rm,w,PB,cam);Lh=null;}
  gl.disable(gl.CULL_FACE);bindMesh(P,PR.m);
  const bc=xf(MbS,[0,62,0]),toCam=nrm(cam.eyeW.map((v,i)=>v-bc[i])),lt=neutral(probe(rm,bc,toCam,.75)),lh=handLight(rm,bc,toCam);   // the light where the box is
  const dr=(n,M)=>draw(P,CASE_T,pp[n].first,pp[n].count,{M:mul(MbS,M),kind:3,tint:tintMul([1.12,1.12,1.12],lt)});
  if(sys==='ps1'){dr('ps1_tray',I4);dr('ps1_lid',hingeAt(Sc,pp.ps1_lid.pivot,rotY(op?op.lidA:0)));}   // never a disc (no disc images)
  else{dr('gba_box',I4);dr('gba_cart',op?op.cart:I4);dr('gba_flap',hingeAt(Sc,pp.gba_flap.pivot,rotZ(op?op.flapA:0)));}
  handsDraw(P,rm,R&&R.slide>=330?null:R,Lh&&Lh.slide>=318?null:Lh,{tint:lh});
  gl.enable(gl.CULL_FACE);
 }

 // ------------------------------------------------------------------ top screen
 const EYEC=[canvas(400,240),canvas(400,240)];
 // debug (bborbit=yaw,pitch[,distance]): look at the held box / card from another angle (the box keeps its place)
 function orbitCam(rm,w,p,cam0,s){
  if(!OPT.orbit)return cam0;let c=null;
  if(w.mode==='lib'&&w.L.p>.002){const PB=pulledBox(rm,w,cam0);c=xf(PB.M,[0,PB.b.H/2,0]);}
  else if(w.share.p>.002){const W=cardCorners(rm,w,cam0);c=[0,1,2].map(i=>(W[0][i]+W[2][i])/2);}
  if(!c)return cam0;const d=p.pos.map((v,i)=>v-c[i]),r=Math.hypot(...d)*(OPT.orbit[2]||1);
  const yaw=Math.atan2(d[0],d[2])+OPT.orbit[0]*Math.PI/180,pit=Math.asin(d[1]/Math.hypot(...d))+OPT.orbit[1]*Math.PI/180;
  const pos=[c[0]+r*Math.cos(pit)*Math.sin(yaw),c[1]+r*Math.sin(pit),c[2]+r*Math.cos(pit)*Math.cos(yaw)];
  return useCamOf({...p,pos,look:c},s);
 }
 function useCamOf(p,s){const[yaw,pitch]=aim(p.pos,p.look);return camera(p.pos,yaw,pitch,p.F,p.eye*s*.5,p.conv);}
 function eyeImage(c,w,s){
  const rm=w.rm,p=curPose(w),cam0=useCamOf(p,s),cam=orbitCam(rm,w,p,cam0,s);
  render(()=>{scene(rm,w,cam,{dust:w.mode!=='lib'||w.L.zoom<.5});if(w.mode==='lib'&&w.L.p>.002)drawPulled(P0,rm,w,cam0);});c.drawImage(GLC,0,0);
  wallLabels(c,w,cam);
  if(w.mode==='lib'){libOverlay(c,w,cam);if(w.open)openFade(c,w);}
  if(w.share.p>.002)heldCard(c,w,cam,cam0);
  if(w.pick.k>.01)pickerHands(c,w,cam);
  focusTag(c,w,cam,s);
  if(w.play>0){const fa=w.play<.55?ease(w.play/.55):1-smooth((w.play-.7)/.3);c.fillStyle=`rgba(255,250,240,${.9*fa})`;c.fillRect(0,0,400,240);}
 }
 // the held QR card and the hand, drawn over the room (own depth buffer)
 // the hand reaches for the card in its stand, closes on its right edge (the edge pinch), then carries it
 // the held card as a box (grip space: x thickness, front +x; y up from its bottom edge; z width, free edge +z)
 function cardFrame(rm,w,cam){const W=cardCorners(rm,w,cam),sub=(a,c)=>a.map((v,i)=>v-c[i]);
  const ez=nrm(sub(W[1],W[0])),ey=nrm(sub(W[1],W[2])),ex=cross(ey,ez),o=W[2].map((v,i)=>(v+W[3][i])/2);
  return{Mc:frameMat({o,ex,ey,ez}),b:{T:2,H:Math.hypot(...sub(W[1],W[2])),W:Math.hypot(...sub(W[1],W[0]))}};}
 function cardSpec(w,cam){
  const rm=w.rm,u=w.share.p,{Mc,b}=cardFrame(rm,w,cam);
  const g=grip(rm,'card',{T:2,H:Math.round(b.H),W:Math.round(b.W)},{gy:.2}),G_=st(g);
  const reach=smooth(u/(CARD_GRAB*.67)),close=smooth((u-.1)/.24);   // same order as the box: slide in, then curl   // a slower close
  const s=closeOn(g,close,25);if(close>=1)s.pose=RH.breathe(s.pose,w.t,.5,IDLE_P);
  return handOf(Mc,s,{slide:(1-reach)*340,chk:[boxIn(b,Mc)]});
 }
 function heldCard(c,w,cam,cam0=cam){
  const rm=w.rm;
  render(()=>{const P=begin(cam,w.t);gl.disable(gl.CULL_FACE);bindMesh(P,rm.m);const p=rm.parts.a_card,M=new Float32Array(NPAL*16);M.set(cardMatrix(rm,w,cam0),11*16);
   lightmapBind(P,rm);draw(P,rm.T[0],p.first,p.count,{pal:{M,T:new Float32Array(NPAL*3).fill(OPT.lm?1.35:1.85)},lm:true});
   const cc=cardCorners(rm,w,cam0)[0];handsDraw(P,rm,cardSpec(w,cam0),null,{tint:handLight(rm,cc,cam0.Fw.map(v=>-v))});},[0,0,0,0],false);
  c.drawImage(GLC,0,0);
 }
 // the hand picker: both hands raised in front of the scope, turning to show palm and back (own depth buffer)
 function pickerHands(c,w,cam){
  if(OPT.dbg==='nohand')return;
  const[R_,L_]=pickerSpecs(w,cam);
  const hp=[0,1,2].map(i=>cam.pos[i]+cam.Fw[i]*700),lt=handLight(w.rm,hp,cam.Fw.map(v=>-v));
  render(()=>{const P=begin(cam,w.t);handsDraw(P,w.rm,R_,L_,{light:OPT.handLight||lt});},[0,0,0,0],false);
  c.drawImage(GLC,0,0);
 }
 function pickerSpecs(w,cam){
  const rm=w.rm,k=w.pick.k,t=w.t,Fw=cam.Fw,U=cam.U,Rv=cam.R;
  // Raised hands, computed: the model's palm normal (anchor hand3) points straight at the camera and
  // its fingers point up the screen at full length; the left hand on screen-left, the right on screen-right,
  // fingertips splayed outward, a slight outward roll (palm . to-camera stays >= 0.98).
  const H3=rm.A.hand3,g1=nrm(H3.slice(0,3)),fd=H3.slice(3,6),g2=nrm(fd.map((v,i)=>v-g1[i]*dot(fd,g1))),g3=cross(g1,g2);
  const rotv=(v,ax,an)=>{const c=Math.cos(an),s_=Math.sin(an),k=nrm(ax),d=dot(k,v),x=cross(k,v);return v.map((q,i)=>q*c+x[i]*s_+k[i]*d*(1-c));};
  const mk=(side,ph,kk)=>{
   const o=[0,1,2].map(i=>cam.pos[i]+Fw[i]*720+Rv[i]*side*108+U[i]*(-46+4*Math.sin(IW*t+ph)));
   let t1=nrm(cam.pos.map((v,i)=>v-o[i])),t2=nrm(U.map((v,i)=>v-t1[i]*dot(U,t1)));
   t2=rotv(t2,t1,-side*(.2+.05*Math.sin(IW*t+ph)));            // splay: fingertips away from the centre
   t1=rotv(t1,t2,side*.15);                                     // slight outward roll
   const t3=cross(t1,t2).map(v=>v*side);                        // side -1: mirrored (the left hand)
   const col=j=>[0,1,2].map(i=>t1[i]*g1[j]+t2[i]*g2[j]+t3[i]*g3[j]);
   const F={o,ex:col(0),ey:col(1),ez:col(2)};
   if(STATS){const pn=[0,1,2].map(i=>t1[i]),tc=nrm(cam.pos.map((v,i)=>v-o[i]));(STATS.palm=STATS.palm||[]).push(+dot(pn,tc).toFixed(3));}
   // the pose: open and relaxed, breathing, and a slow wave runs across the fingers (pinky to index)
   const p=RH.breathe(RH.vary(RH.POSES.show,side>0?3:8,.07),t+ph,1,IDLE_P);['pinky','ring','middle','index'].forEach((d,i)=>{const a=.09*Math.max(0,Math.sin(IW*t+ph-i*.55));RH.CH(d).forEach((kk,j)=>{p[kk*2]+=a*[1,1.2,.8][j];});});
   // the wrist sways a little (each hand at its own pace), the fingers follow a moment later (they are in the pose)
   const sw=mul(T4(...o),mul(rotAx(t1,.035*Math.sin(IW*t+ph*2)),mul(rotAx(t2,.025*Math.sin(2*IW*t+ph)),T4(-o[0],-o[1],-o[2]))));
   return{M:mul(sw,frameMat(F)),pose:p,slide:(1-smooth(kk/.8)-.04*Math.sin(Math.PI*clamp((kk-.6)/.4))**2)*420};};
  const ks=w.pick.s||0;                                           // time-based, a soft overshoot at the end
  return[mk(1,0,clamp(ks/.9)),mk(-1,1.7,clamp((ks-.1)/.9))];   // The left hand enters a beat later and leaves first; each hand overshoots and settles.
 }
 // the library's 2D bits on the top screen: the letter while scrolling fast, the row's name
 function libOverlay(c,w,cam){
  const L=w.L,row=LIBROWS[L.row];if(L.level!=='shelf'||!row.games.length)return;
  const a=clamp(1-(w.t-L.letterT-.5)/.4)*smooth(L.zoom);if(a<=0)return;
  const g=row.games[row.sel],P_=R8.room().paper;
  c.save();c.globalAlpha=a;c.translate(30,26);c.rotate(-.03);
  c.fillStyle='rgba(0,0,0,.45)';c.fillRect(3,4,44,50);c.fillStyle=lg(c,0,0,0,50,[[0,P_[0]],[1,P_[1]]]);c.fillRect(0,0,44,50);
  c.fillStyle='rgba(150,40,30,.85)';c.fillRect(0,0,44,3);c.fillStyle='#1e1208';c.font=`700 34px ${SERIF}`;c.textAlign='center';c.fillText(g.letter,22,40);
  c.font=`700 9px ${TYPE}`;c.fillStyle='#e8d8b8';c.textAlign='left';c.fillText((row.sel+1)+' / '+row.games.length,0,66);c.restore();
 }
 function fadeAlpha(w){const o=w.open;if(!o)return 0;const e=openT(w),sys=SYS[o.g.sys].prop,f0=OPEN[sys].fade,OT=OPEN[sys].len;
  if(!o.done)return Math.pow(smooth((e-f0)/(OT-f0)),1.3);const r=w.t-o.t1;return r<.9?1:1-smooth((r-.9)/BACK_T);}
 function openFade(c,w){
  const o=w.open;let a=fadeAlpha(w);
  if(a>0){c.fillStyle=`rgba(0,0,0,${a})`;c.fillRect(0,0,400,240);}
  if(o.done&&w.t-o.t1<.9){c.fillStyle='rgba(240,226,196,.8)';c.font=`700 11px ${TYPE}`;c.textAlign='center';c.fillText('Starting '+o.g.title+'…',200,124);c.fillText('(mock: the game would start here)',200,140);c.textAlign='left';}
 }
 // ---- the focus tag: a paper label on a string over the focused place, kept inside the screen
 function focusTag(c,w,cam,s){
  if(w.mode!=='home')return;const id=w.focus,a=w.rm.A['focus.'+id],p=cam.proj(...a);if(p[2]<=NEAR)return;
  const k=smooth((w.t-w.focusT)/.35),label=NAME[id],sub=SUB(w,id);
  c.save();c.font=`700 15px ${SERIF}`;const lw=c.measureText(label).width;c.font=`700 10px ${TYPE}`;const sw=sub?c.measureText(sub).width:0;
  const tw=Math.max(lw,sw)+26,th=sub?42:28,x=clamp(p[0],tw/2+6,400-tw/2-6),y=clamp(p[1]-58-8*(1-k),8,240-th-8),sw_=.04*Math.sin(w.t*1.6);
  c.globalAlpha=k;
  c.strokeStyle='rgba(240,226,196,.75)';c.lineWidth=1;c.beginPath();c.moveTo(x,y+th);c.lineTo(p[0],p[1]-6);c.stroke();
  c.fillStyle='rgba(255,236,200,.9)';c.beginPath();c.arc(p[0],p[1]-6,2,0,Math.PI*2);c.fill();
  c.translate(x,y);c.rotate(sw_);
  c.fillStyle='rgba(0,0,0,.45)';c.fillRect(-tw/2+3,4,tw,th);
  const P_=R8.room().paper;c.fillStyle=lg(c,0,0,0,th,[[0,P_[0]],[1,P_[1]]]);c.fillRect(-tw/2,0,tw,th);
  c.fillStyle='rgba(150,40,30,.85)';c.fillRect(-tw/2,0,tw,3);
  c.fillStyle='#1e1208';c.font=`700 15px ${SERIF}`;c.textAlign='center';c.fillText(label,0,20);
  if(sub){c.font=`700 10px ${TYPE}`;c.fillStyle='#7a2a1c';c.fillText(sub,0,34);}
  c.restore();
 }
 const SUB=(w,id)=>({continue:'Silent Hill',library:(()=>{const o=LIBROWS.filter(r=>r.sid!=='fav');return o.reduce((a,r)=>a+r.games.length,0)+' games · '+o.length+' systems';})(),settings:'CH'+(w.tuneSel+1)+' '+CHANNELS[w.tuneSel].label.toLowerCase(),together:'2 nearby',share:''}[id]);
 function drawTop(c,w,mode){
  if(!mode||mode==='mono'){eyeImage(c,w,0);return;}
  if(mode==='L'||mode==='R'){eyeImage(c,w,mode==='L'?-1:1);return;}
  const imgs=EYEC.map((cv,i)=>{const k=cv.getContext('2d');k.fillStyle='#000';k.fillRect(0,0,400,240);eyeImage(k,w,i?1:-1);return k.getImageData(0,0,400,240);});
  const L=imgs[0].data,R=imgs[1].data;for(let i=0;i<L.length;i+=4)R[i]=Math.min(255,(L[i]*.299+L[i+1]*.587+L[i+2]*.114)*1.15);
  const k=EYEC[0].getContext('2d');k.putImageData(imgs[1],0,0);c.drawImage(EYEC[0],0,0);
 }

 // ------------------------------------------------------------------ bottom screen
 const INK=R8.INK,INK_HI=R8.INK_HI;
 const SCOPE_ICON=(c,x,y,s,col)=>{c.strokeStyle=col;c.lineWidth=1.5;R8.rr(c,x-s*.62,y-s*.42,s*1.24,s*.84,2);c.stroke();c.beginPath();
  for(let i=0;i<=16;i++){const f=i/16,xx=x-s*.42+f*s*.62,yy=y+Math.sin(f*Math.PI*2)*s*.22;i?c.lineTo(xx,yy):c.moveTo(xx,yy);}c.stroke();
  c.fillStyle=col;c.beginPath();c.arc(x+s*.42,y-s*.08,s*.1,0,Math.PI*2);c.fill();c.beginPath();c.arc(x+s*.42,y+s*.2,s*.07,0,Math.PI*2);c.fill();};
 const ACTIONS={settings:[{key:'B',label:'Room',icon:'back'},{key:'UP',label:'Channel',icon:'scope'},{key:'A',label:'Change',icon:'open'}],
  wall:[{key:'B',label:'Room',icon:'back'},{key:'X',label:'Sort',icon:'sort'},{key:'Y',label:'Rescan',icon:'rescan'},{key:'A',label:'Open',icon:'open'}],
  shelf:[{key:'B',label:'Shelves',icon:'back'},{key:'Y',label:'Favorite',icon:'star'},{key:'L',label:'A-Z jump',icon:'letters'},{key:'A',label:'Take out',icon:'open'}],
  picker:[{key:'B',label:'Done',icon:'back'},{key:'UP',label:'Row',icon:'scope'},{key:'RIGHT',label:'Change',icon:'open'}],
  pulled:[{key:'B',label:'Put back',icon:'back'},{key:'Y',label:'Favorite',icon:'star'},{key:'X',label:'Options',icon:'sort'},{key:'A',label:'Play',icon:'play'}]};
 function bar(c,w){
  const T=R8.textures();
  c.drawImage(T.desk,0,150,320,32,0,208,320,32);c.fillStyle='rgba(12,5,1,.55)';c.fillRect(0,208,320,32);
  c.fillStyle=lg(c,0,208,0,211,[[0,'rgba(232,190,120,.75)'],[1,'rgba(232,190,120,0)']]);c.fillRect(0,208,320,3);
  const L=w.L,lib=w.mode==='lib',mode=lib?(L.level==='wall'?'wall':L.pTo===1&&L.p>.5?'pulled':'shelf'):w.mode==='at'?(w.place==='settings'&&w.pick.on?'picker':w.place):null;
  const items=mode&&ACTIONS[mode]?ACTIONS[mode]:mode&&R9.ACTIONS[mode]?R9.ACTIONS[mode]:R9.TABS.map(t=>({tab:t}));
  items.forEach((b,i)=>{
   const x=Math.round(320*i/items.length),bw=Math.round(320*(i+1)/items.length)-x,on=b.tab?b.tab.id===(w.mode==='home'||w.mode==='back'?w.focus:w.place):b.key==='A';
   if(on){c.fillStyle=lg(c,0,208,0,240,[[0,'rgba(196,130,62,.55)'],[1,'rgba(120,70,30,.4)']]);c.fillRect(x,208,bw,32);}else if(i){c.fillStyle='rgba(240,210,160,.2)';c.fillRect(x,212,1,24);}
   const ink=on?INK_HI:INK,css=`rgb(${ink.join(',')})`,icon=b.tab?(b.tab.id==='settings'?'scope':b.tab.icon):b.icon;
   const fav=icon==='star'&&lib&&(()=>{const r=LIBROWS[L.row],g=r.games[r.sel];return g&&L.favs.has(g.sys+'/'+g.title);})();
   if(icon==='scope')SCOPE_ICON(c,x+bw/2,217,11,css);else if(R9.ICON[icon])R9.ICON[icon](c,x+bw/2,217,11,css);else if(BarIcons[icon])BarIcons[icon](x+bw/2,217,11,ink,fav);
   c.font=`600 10.8px 'NotoSans-Regular',sans-serif`;c.textAlign='center';c.fillStyle=css;c.fillText(b.tab?b.tab.label:b.label,x+bw/2,235);c.textAlign='left';
  });
  return items;
 }
 const SETROWS=Object.assign({},R9.SETROWS,{room:[['Room style',null],['3D depth','Normal']],hand:[['Skin tone',null],['Gloves',null]]});
 function desk(c){const sid=R8.room().id;const T=R8.textures();c.drawImage(T.desk,0,0);
  const g=c.createRadialGradient(70,40,10,120,90,330);g.addColorStop(0,`rgba(${R8.room().light},.16)`);g.addColorStop(1,`rgba(0,0,0,${sid==='apartment'?.18:.5})`);c.fillStyle=g;c.fillRect(0,0,320,208);}
 function rows(k,list,y0){k.font=`700 10.5px ${TYPE}`;k.fillStyle='#3e2a16';list.forEach(([a,b],i)=>{k.fillText(a,48,y0+i*16);k.fillText(b,164,y0+i*16);});}
 function bottomSettings(c,w,sid){
  const here=w.mode==='at'&&w.place==='settings',ch=CHANNELS[w.tuneSel];desk(c);
  if(here&&w.pick.on){bottomPicker(c,w,sid);return;}
  R8.card(c,22,18,276,180,-.012,k=>{
   k.font=`700 10px ${TYPE}`;k.fillStyle='#7a2a1c';k.fillText('THE OSCILLOSCOPE',48,16);k.textAlign='right';k.fillText(here?'YOU ARE HERE':'BREADBOX',266,16);k.textAlign='left';
   k.font=`700 19px ${SERIF}`;k.fillStyle='#1e1208';k.fillText(here?'CH'+(w.tuneSel+1)+' · '+ch.label[0]+ch.label.slice(1).toLowerCase():'Settings',48,44);
   // the five channels as a strip of buttons, the chosen one lit
   CHANNELS.forEach((c5,i)=>{const x=48+i*36,on=i===w.tuneSel;k.fillStyle=on?'rgba(60,150,90,.85)':'rgba(70,50,30,.18)';k.fillRect(x,52,33,13);
    k.font=`700 6.5px ${TYPE}`;k.fillStyle=on?'#eaffea':'#4a3622';k.textAlign='center';k.fillText(c5.label.slice(0,7),x+16.5,61.5);k.textAlign='left';});
   const list=(SETROWS[ch.id]||[]).map(([a,b])=>[a,b||(a==='Skin tone'?SKIN_NAME[+OPT.skin-1]:a==='Gloves'?GLOVE_NAME[GLOVES[OPT.glove]]+' · '+HANDNAME[sid]+' sleeve':R9.STYLE_NAME[sid])]);rows(k,list,86);
   k.font=`700 10px ${TYPE}`;k.fillStyle='#5a3a1c';k.fillText(here?'Turn the knob: up / down · trace: '+TRACE_NAME[w.tuneSel]:'A scope on the desk: pick a channel',48,150);
  });
 }
 const HANDNAME={rustic:'flannel',apartment:'knit',night:'pajamas',nineties:'hoodie'};
 const SKIN_CSS=['#f2e2d6','#d7b59a','#a8805f','#6e5038'];
 function bottomPicker(c,w,sid){
  const P_=w.pick;
  R8.card(c,22,18,276,180,-.012,k=>{
   k.font=`700 10px ${TYPE}`;k.fillStyle='#7a2a1c';k.fillText('CH6 · HAND',48,16);k.textAlign='right';k.fillText('LIVE PREVIEW ON TOP',266,16);k.textAlign='left';
   k.font=`700 19px ${SERIF}`;k.fillStyle='#1e1208';k.fillText('Your hands',48,42);
   const rowY=[66,112];
   rowY.forEach((y,r)=>{if(P_.row===r){k.fillStyle='rgba(176,120,40,.22)';k.fillRect(44,y-12,222,40);k.fillStyle='#8a5a1c';k.beginPath();k.moveTo(32,y-6);k.lineTo(40,y-2);k.lineTo(32,y+2);k.fill();}
    k.font=`700 10px ${TYPE}`;k.fillStyle='#3e2a16';k.fillText(r?'GLOVES':'SKIN TONE',48,y);});
   SKIN_CSS.forEach((col,i)=>{const x=48+i*36,on=+OPT.skin===i+1;k.fillStyle=col;k.fillRect(x,72,28,14);if(on){k.strokeStyle='#1e1208';k.lineWidth=2;k.strokeRect(x-2,70,32,18);}});
   k.font=`700 9px ${TYPE}`;k.fillStyle='#5a3a1c';k.fillText(SKIN_NAME[+OPT.skin-1],196,83);
   GLOVES.forEach((g,i)=>{const x=48+i*44,on=OPT.glove===i;k.font=`${on?700:400} 9px ${TYPE}`;k.fillStyle=on?'#1e1208':'#7a6650';k.fillText(GLOVE_NAME[g].replace(' gloves','').replace(' hands',''),x,130);
    if(on){k.fillStyle='#8a5a1c';k.fillRect(x,133,36,2);}});
   k.font=`700 9px ${TYPE}`;k.fillStyle='#5a3a1c';k.fillText('◀ ▶ change · ▲ ▼ row · sleeve: '+HANDNAME[sid]+' (comes with the room)',48,156);
  });
 }
 function bottomLibrary(c,w){
  const L=w.L;desk(c);
  if(w.mode!=='lib'||L.level==='wall'){
   // the case as a grid: 3 sections per row, only owned systems (packed from the top left), 24 slots
   const nb=w.rm.CASE.nbay,rowsN=Math.ceil(LIBROWS.length/nb),hr=Math.floor(L.hsel/nb),r0=clamp(hr-3,0,Math.max(0,rowsN-7));
   R8.card(c,22,14,276,186,-.012,k=>{
    k.font=`700 10px ${TYPE}`;k.fillStyle='#7a2a1c';k.fillText('CATALOGUE',48,16);k.textAlign='right';k.fillText(LIBROWS.length+' OF 24 SLOTS',266,16);k.textAlign='left';
    k.font=`700 19px ${SERIF}`;k.fillStyle='#2a1a0c';k.fillText('Your shelves',48,40);
    for(let r=r0;r<Math.min(rowsN,r0+7);r++)for(let cc=0;cc<nb;cc++){const sec=LIBROWS[r*nb+cc];if(!sec)continue;
     const x=44+cc*74,y=52+(r-r0)*18,on=sec.i===L.hsel&&(w.mode==='lib'||w.focus==='library'),sy=SYS[sec.sid];
     k.fillStyle=on?'rgba(176,120,40,.32)':'rgba(70,50,30,.08)';k.fillRect(x,y,70,16);if(on){k.strokeStyle='#8a5a1c';k.lineWidth=1.5;k.strokeRect(x+.5,y+.5,69,15);}
     k.font=`${on?700:600} 9.5px ${SANS}`;k.fillStyle=on?'#1e1208':'#3e2a16';k.fillText((sec.sid==='fav'?'★ ':'')+sy.short,x+4,y+11.5);
     k.font=`700 7px ${TYPE}`;k.textAlign='right';k.fillStyle='#7a5a3a';k.fillText(String(sec.games.length),x+67,y+11.5);k.textAlign='left';}
    k.font=`700 9px ${TYPE}`;k.fillStyle='#5a3a1c';k.fillText('◀ ▶ ▲ ▼ pick a system · A: walk up to it',48,183);
   });
   return;}
  const row=LIBROWS[L.row],g=row.games[row.sel],n=row.games.length;
  // the row at scale: every spine as a tick, the letters underneath, a brass pointer
  c.fillStyle='rgba(0,0,0,.42)';c.fillRect(8,4,304,40);
  const tot=row.games[n-1].lx+20;
  for(const q of row.games){const x=12+q.lx/tot*296;c.fillStyle=miniColour(q);c.fillRect(x,22-q.b.H*.1,Math.max(.6,q.b.T/tot*296),q.b.H*.1);}
  let last='';c.font=`700 7px ${TYPE}`;c.fillStyle='rgba(234,212,174,.8)';c.textAlign='center';
  for(const q of row.games)if(q.letter!==last){last=q.letter;const x=12+q.lx/tot*296;c.fillText(q.letter,x,32);}
  c.textAlign='left';const px=12+g.lx/tot*296;c.fillStyle='#f0c070';c.beginPath();c.moveTo(px,34);c.lineTo(px-3,41);c.lineTo(px+3,41);c.fill();
  c.fillStyle='rgba(240,192,112,.35)';const vw=(L.speed>.1?640:340)/tot*296;c.fillRect(px-vw/2,8,vw,16);
  const fav=L.favs.has(g.sys+'/'+g.title);
  R8.card(c,22,50,276,150,-.014,k=>{
   k.font=`700 10px ${TYPE}`;k.fillStyle='#7a2a1c';k.fillText(SYS[row.sid].name.toUpperCase(),48,16);
   k.textAlign='right';k.fillText((row.sel+1)+' / '+n,266,16);k.textAlign='left';
   k.font=`700 ${R8.fitFont(k,g.title,214,19,SERIF)}px ${SERIF}`;k.fillStyle='#1e1208';k.fillText(g.title,48,43);
   k.font=`700 10px ${TYPE}`;k.fillStyle='#3e2a16';const h=R8.ih(g.i,row.r,3);
   [['System',SYS[g.sys].name],['Last played',['yesterday','2 days ago','last week','in March','never'][Math.floor(h*5)]],['Play time',Math.floor(h*40)+' h']].forEach(([a,b],i)=>{k.fillText(a,48,66+i*14);k.fillText(b,150,66+i*14);});
   if(fav)R8.stamp(k,218,96,'FAVORITE ★',.82);
   k.font=`700 9px ${TYPE}`;k.fillStyle='#5a3a1c';k.fillText(L.pTo===1?'L / R: turn the box · A: play':'Hold ◀ ▶ to fly along the shelf · L / R: A–Z',48,128);
  });
 }
 function drawBottom(c,w,sid){
  const id=w.mode==='home'||w.mode==='back'?w.focus:w.place||w.focus;
  if(w.mode==='lib'||id==='library')bottomLibrary(c,w);
  else if(id==='settings')bottomSettings(c,w,sid);
  else R9.bottom(c,w);
  return bar(c,w);
 }

 // ------------------------------------------------------------------ scripted runs (gallery films)
 const SCRIPTS={
  // the whole menu; every way back is a glide: TV, cushions, coffee table, scope, then the shelves
  tour:{focus:'continue',dur:38.6,keys:[[.9,'A'],[3.1,'B'],[5.0,'DOWN'],[5.3,'A'],[7.1,'RIGHT'],[7.9,'B'],[9.7,'RIGHT'],[10.0,'A'],[13.3,'B'],
   [15.6,'UP'],[15.9,'RIGHT'],[16.2,'A'],[17.9,'DOWN'],[18.6,'DOWN'],[19.3,'DOWN'],[20.2,'B'],[22.0,'LEFT'],[22.3,'A'],
   [24.6,'A'],[26.4,'RIGHT'],[26.9,'RIGHT'],[28.4,'HOLDR'],[30.4,'REL'],[33.6,'B'],[34.4,'B'],[35.9,'B']]},
  // fast scrolling through the 315 GBA games: enter the row, hold right, stop, take one out, hold left, back
  scroll:{focus:'library',place:'library',dur:19,keys:[[.5,'LEFT'],[1.1,'A'],[2.8,'HOLDR'],[8.4,'REL'],[10.6,'L'],[11.0,'REL'],[12.4,'HOLDL'],[15.6,'REL'],[17.2,'B']]},
  // the game start, both cases
  start:{focus:'library',place:'library',dur:21,keys:[[.3,'A'],[1.95,'A'],[5.2,'A'],[8.9,'B'],[11.6,'B'],[12.6,'LEFT'],[13.3,'A'],[15.0,'A'],[18.0,'A']]},   // A takes the box
  // Settings: the scope's five channels, then the ROOM channel changes the style
  scope:{focus:'settings',place:'settings',dur:10,keys:[[1.2,'DOWN'],[2.6,'DOWN'],[4.0,'DOWN'],[5.4,'DOWN'],[7.0,'UP'],[8.3,'UP']]},
  style:{focus:'settings',place:'settings',dur:7,keys:[[.3,'DOWN'],[.6,'DOWN'],[.9,'DOWN'],[1.2,'DOWN'],[2.4,'A'],[4.4,'A']]},
  idle:{focus:'continue',dur:4,keys:[]},
  held:{focus:'library',place:'library',dur:5,keys:[[.3,'A'],[1.95,'A']]},
  heldgba:{focus:'library',place:'library',dur:5.4,keys:[[.2,'LEFT'],[.7,'A'],[2.35,'A']]},
  wall:{focus:'library',place:'library',dur:3,keys:[]},
  walltop:{focus:'library',place:'library',dur:3.5,keys:[[.3,'UP'],[.6,'UP'],[.9,'UP'],[1.2,'UP'],[1.5,'UP'],[1.8,'UP']]},
  pickerhold:{focus:'settings',place:'settings',dur:6,keys:[[.3,'DOWN'],[.5,'DOWN'],[.7,'DOWN'],[.9,'DOWN'],[1.1,'DOWN'],[1.6,'A']]},
  // take a box out (the hand reaches in, pulls, turns it, re-grips), turn it in the hand, put it back
  pick:{focus:'library',place:'library',dur:13,keys:[[.3,'A'],[1.95,'A'],[5.2,'R'],[6.4,'R'],[7.6,'L'],[8.8,'L'],[10.0,'B']]},   // A takes the box (no auto take-out)
  // the same on the GBA shelf
  pickgba:{focus:'library',place:'library',dur:13.4,keys:[[.2,'LEFT'],[.7,'A'],[2.35,'A'],[5.6,'R'],[6.8,'R'],[8.0,'L'],[9.2,'L'],[10.4,'B']]},
  // the QR card: reach, take, lift, zoom, put back
  card:{focus:'share',place:'share',dur:9,keys:[[.5,'A'],[3.9,'A'],[5.4,'A'],[6.6,'B']]},
  // Settings CH6 HAND: try four skin tones and five gloves
  picker:{focus:'settings',place:'settings',dur:15,keys:[[.5,'DOWN'],[.9,'DOWN'],[1.3,'DOWN'],[1.7,'DOWN'],[2.1,'DOWN'],[2.9,'A'],[4.4,'RIGHT'],[5.4,'RIGHT'],[6.4,'RIGHT'],[7.3,'DOWN'],[7.8,'RIGHT'],[8.9,'RIGHT'],[10,'RIGHT'],[11.1,'RIGHT'],[12.3,'UP'],[12.6,'RIGHT'],[13.8,'B']]},
  // the 24-slot case: move on the wall, walk up to a section (real spines cross-fade in), back, another one
  case:{focus:'library',place:'library',dur:13,keys:[[.8,'LEFT'],[1.4,'UP'],[2.0,'RIGHT'],[2.6,'A'],[4.0,'RIGHT'],[4.6,'RIGHT'],[5.6,'B'],[7.2,'DOWN'],[7.7,'LEFT'],[8.2,'A'],[9.6,'LEFT'],[11.2,'B']]},
  smoke:{focus:'continue',dur:34,keys:[[.5,'A'],[2.3,'A'],[4.2,'B'],[6,'DOWN'],[6.3,'A'],[8,'RIGHT'],[8.5,'LEFT'],[9,'A'],[9.3,'B'],[11,'RIGHT'],[11.3,'A'],[13.3,'Y'],[13.6,'UP'],[14,'A'],[14.5,'B'],[16,'B'],[17.4,'B'],
   [19.5,'LEFT'],[19.8,'SELECT'],[21.6,'DOWN'],[21.9,'DOWN'],[22.2,'B'],[24,'UP'],[24.3,'A'],[25.6,'A'],[27,'R'],[27.5,'R'],[28,'A'],[29,'A'],[30,'B'],[31,'B'],[32.4,'B']]}};

 class Screen{
  constructor(opt={}){this.opt=Object.assign({eye:'mono',script:null,style:'rustic',focus:null},opt);this.setStyle(this.opt.style);this.w=this.fresh();this.capture=false;this.acc=0;this.clock=0;this.sim=null;
   this.title='BreadBox';this.source='Round 10 · the 3D room (round 3)';}
  setStyle(id){this.sid=STYLE_IDS.includes(id)?id:'rustic';R9.setStyle(this.sid);this.rm=room(this.sid);STRIPMESH=null;SHELF_KEY=null;
   if(this.w)this.w.rm=this.rm;if(this.sim)this.sim.w.rm=this.rm;}
  fresh(){const sc=this.opt.script&&SCRIPTS[this.opt.script],w=world(this.rm,this.opt.focus||(sc&&sc.focus)||'continue');
   const pl=this.opt.place||(sc&&sc.place);if(pl){w.focus=pl;w.mode=pl==='library'?'lib':'at';w.place=pl;w.t0=-5;}
   w.onStart=g=>{if(!this.capture)App.toast('Mock only · would start '+g.title);};return w;}
  update(){if(this.capture)return;this.acc+=1/60;
   const L=this.w.L,held=App.held||{};
   if(this.w.mode==='lib'&&L.level==='shelf'){const h=held.RIGHT!==undefined?1:held.LEFT!==undefined?-1:0;if(h&&L.hold!==h){L.hold=h;L.holdT=this.w.t;L.fsel=LIBROWS[L.row].sel;}if(!h&&L.hold)L.hold=0;}
   while(this.acc>=DT){step(this.w,DT);this.acc-=DT;}const d=new Date();this.w.clock=[d.getHours(),d.getMinutes()];
   if(this.w.styleReq){this.w.styleReq=0;this.setStyle(STYLE_IDS[(STYLE_IDS.indexOf(this.sid)+1)%STYLE_IDS.length]);this.opt.style=this.sid;App.toast('Room: '+R9.STYLE_NAME[this.sid]);}}
  view(){if(!this.capture)return this.w;const sc=SCRIPTS[this.opt.script]||{keys:[]};
   if(!this.sim||this.clock<this.sim.w.t-1e-6){this.setStyle(this.opt.style);this.sim={w:this.fresh(),s:0};this.sim.w.sim=true;}
   const S_=this.sim;while(S_.w.t<this.clock-1e-9){while(S_.s<sc.keys.length&&sc.keys[S_.s][0]<=S_.w.t+1e-9){act(S_.w,sc.keys[S_.s][1]);S_.s++;}step(S_.w,DT);
    if(S_.w.styleReq){S_.w.styleReq=0;this.setStyle(STYLE_IDS[(STYLE_IDS.indexOf(this.sid)+1)%STYLE_IDS.length]);}}
   return S_.w;}
  key(k){if(this.capture)return;const w=this.w;
   if(w.mode==='at'&&w.place==='continue'&&k==='A')setTimeout(()=>App.toast('Mock only · would continue Silent Hill'),1300);
   if(w.mode==='at'&&w.place==='together'&&k==='A'){App.toast('Mock only · would '+(w.tog.sel?'join a game':'host a game'));return;}
   act(w,k);}
  touch(x,y){if(this.capture)return;const w=this.w;
   if(y>=208){const items=this.items||[],b=items[Math.min(items.length-1,Math.floor(x*items.length/320))];if(!b)return;
    if(b.tab){if(w.mode==='home'){if(w.focus===b.tab.id)act(w,'A');else{w.focus=b.tab.id;w.focusT=w.t;}}return;}act(w,b.key);return;}
   if(w.mode==='lib'&&w.L.level==='shelf'&&y<44){const row=LIBROWS[w.L.row],tot=row.games[row.games.length-1].lx+20;let best=0,bd=1e9;
    row.games.forEach((q,i)=>{const d=Math.abs(12+q.lx/tot*296-x);if(d<bd){bd=d;best=i;}});if(w.L.pTo===1)pull(w.L,0,.4,w.t);row.sel=best;w.L.fsel=best;w.L.lastMove=w.t;w.L.arm=true;return;}
   if(w.mode==='home'||w.mode==='lib')act(w,'A');}
  drawTop(){const c=C2D.get(),w=this.view();R9.setStyle(this.sid);STATS={draws:0,tris:0,boxes:0};const l0=RING.loads,p0=STRIP_PAINTS;drawTop(c,w,this.opt.eye);
   STATS.spineLoads=RING.loads-l0;STATS.stripPaints=STRIP_PAINTS-p0;this.topCost=STATS;window.BB10PALM=STATS.palm||null;STATS=null;}
  drawBottom(){const c=C2D.get(),w=this.view();this.items=drawBottom(c,w,this.sid);this.report(w);}
  report(w){const el=document.getElementById('bb10cost')||(()=>{const p=document.createElement('pre');p.id='bb10cost';p.style.display='none';document.body.appendChild(p);return p;})();
   const mode=w.open?'open':w.mode==='lib'?(w.L.level==='wall'&&w.L.zoom<.01?'wall':w.L.p>.01?'pulled':w.L.speed>.25?'shelf-fast':'shelf'):w.mode==='at'?w.place:w.mode,all=this.costs=this.costs||{};
   const m=all[mode]=all[mode]||{frames:0,draws:0,tris:0,maxDraws:0,maxTris:0,maxBoxes:0,spineLoads:0,maxSpineLoads:0,stripPaints:0},C=this.topCost||{};
   m.frames++;m.draws+=C.draws||0;m.tris+=C.tris||0;m.maxDraws=Math.max(m.maxDraws,C.draws||0);m.maxTris=Math.max(m.maxTris,C.tris||0);m.maxBoxes=Math.max(m.maxBoxes,C.boxes||0);
   m.maxPen=Math.max(m.maxPen||0,+(C.pen||0).toFixed(2));m.u=+w.L.p.toFixed(3);m.t=+w.t.toFixed(3);m.spineLoads+=C.spineLoads||0;m.maxSpineLoads=Math.max(m.maxSpineLoads,C.spineLoads||0);m.stripPaints+=C.stripPaints||0;
   el.textContent='BB10COST '+JSON.stringify({palm:window.BB10PALM||null,modes:all,load:Object.fromEntries(Object.values(ROOMS).map(r=>[r.id,{ms:+r.loadMs.toFixed(1),bytes:r.f.bytes,tris:r.f.nI/3,verts:r.f.nV}]))});}
 }
 function open(opt={}){const s=new Screen(opt);App.push(s);return s;}
 const hash=App.applyHash.bind(App);App.applyHash=function(){hash();const q=new URLSearchParams(location.hash.slice(1));
  if(q.has('bbplayer'))return;          // the hands' reference player (js/room3d-player.js) starts itself
  if(q.has('bbpen')){initGL();library(room('rustic'));const B=bakeAll(),pre=document.createElement('pre');pre.id='bake';pre.textContent=JSON.stringify([...B.pen,...Object.entries(PATHS).map(([k,v])=>['path',k,+v.m.toFixed(2),JSON.stringify(v.d),v.dist])]);document.body.appendChild(pre);return;}
  if(q.has('bbmotion')){MOTION_TRACE=q.get('bbtrace');FRAMESTAT=q.has('bbframe');initGL();library(room('rustic'));const pre=document.createElement('pre');pre.id='bake';pre.textContent=JSON.stringify(bakeAll(+q.get('bbmotion')||2,true));document.body.appendChild(pre);return;}
  if(q.has('bbbake')){initGL();library(room('rustic'));const pre=document.createElement('pre');pre.id='bake';pre.textContent=JSON.stringify(bakeAll());document.body.appendChild(pre);return;}
  if(q.has('bb10')){if(q.get('nops1')==='1')OPT.ps1=false;if(q.has('bborbit'))OPT.orbit=q.get('bborbit').split(',').map(Number);if(q.has('bblm'))OPT.lm=+q.get('bblm');if(q.has('bbvig'))OPT.vig=+q.get('bbvig');if(q.has('bbgrain'))OPT.grain=+q.get('bbgrain');if(q.has('bblmnear'))OPT.lmNearest=+q.get('bblmnear');if(q.has('dither'))OPT.dither=+q.get('dither');if(q.has('bbsys')&&SETS[q.get('bbsys')]){SYSSET=q.get('bbsys');LIBROWS.length=0;SHELF_KEY=null;STRIPMESH=null;}if(q.has('bbskin'))OPT.skin=q.get('bbskin');if(q.has('bbdbg'))OPT.dbg=q.get('bbdbg');if(q.has('bbthick'))OPT.thick=+q.get('bbthick');if(q.has('bbglove'))OPT.glove=Math.max(0,GLOVES.indexOf(q.get('bbglove')));
   // settings-radio stills: bbnolab=1 hides the baked channel names (the design page draws its own);
   // bbkeys=focus,place|t:KEY;t:KEY a one-off key script (record mode)
   if(q.get('bbnolab')==='1')OPT.nolab=1;if(q.has('bbsetshift'))OPT.setShift=q.get('bbsetshift').split(',').map(Number);
   if(q.has('bbkeys')){const[fp,ks]=q.get('bbkeys').split('|'),[fo,pl]=fp.split(',');SCRIPTS.custom={focus:fo,place:pl||undefined,dur:30,keys:(ks||'').split(';').filter(Boolean).map(x=>{const[t,k]=x.split(':');return[+t,k];})};q.set('bbscript','custom');}
   const s=open({style:q.get('bbroom')||'rustic',eye:q.get('bbeye')||'mono',script:q.get('bbscript'),focus:q.get('bbfocus'),place:q.get('bbplace')});s.capture=q.get('bbcapture')==='1';}};
 const panel=App.buildPanel.bind(App);App.buildPanel=function(){panel();if(document.getElementById('bb10-panel'))return;const e=document.createElement('section');e.id='bb10-panel';
  e.innerHTML='<h2>BreadBox X · The 3D room (round 3)</h2><p>Real 3D: PS1-style models made in Blender, drawn with WebGL at 400x240. D-pad: focus · X = A (go there) · Z = B (back). '+
   'Library: ↑ ↓ pick a system row, A walks in; hold ◀ ▶ to fly along the shelf; Q/W (L/R) jump A–Z or turn the held box; A on a held box = Play. Settings: ↑ ↓ turn the scope knob; CH6 HAND, A = try on skin tones and gloves.</p>'+
   '<p>Room <button data-st="rustic">rustic</button> <button data-st="apartment">apartment</button> <button data-st="night">night</button> <button data-st="nineties">90s</button></p>'+
   '<p>Hand <button data-sk="1">light</button> <button data-sk="2">medium</button> <button data-sk="3">dark</button> <button data-sk="4">deep</button> · gloves '+GLOVES.map((g,i)=>`<button data-gl="${i}">${g}</button>`).join(' ')+'</p>'+
   '<p>Library <button data-ss="two">PS1 + GBA</button> <button data-ss="five">5 systems</button> <button data-ss="all">all 24</button></p>'+
   '<p>3D view <button data-eye="mono">2D</button> <button data-eye="ana">red/cyan</button> <button data-eye="L">left eye</button> <button data-eye="R">right eye</button> · <button data-ps1="1">PS1 look on/off</button></p>'+
   '<a href="shots/breadbox-room3d-r3/">round 3 gallery</a> · <a href="shots/breadbox-room3d-r2/">round 2</a> · <a href="shots/breadbox-room3d/">round 1</a>';
  document.getElementById('panel').prepend(e);
  const cur=()=>App.top() instanceof Screen?App.top():null;
  e.querySelectorAll('[data-st]').forEach(b=>b.onclick=()=>{const s=cur();if(s){s.setStyle(b.dataset.st);s.opt.style=s.sid;}else open({style:b.dataset.st});});
  e.querySelectorAll('[data-sk]').forEach(b=>b.onclick=()=>{OPT.skin=b.dataset.sk;});
  e.querySelectorAll('[data-ss]').forEach(b=>b.onclick=()=>{SYSSET=b.dataset.ss;LIBROWS.length=0;SHELF_KEY=null;STRIPMESH=null;RING.cell.clear();RING.slot.fill(null);const s=cur();if(s){s.w=s.fresh();}});
  e.querySelectorAll('[data-gl]').forEach(b=>b.onclick=()=>{OPT.glove=+b.dataset.gl;});
  e.querySelectorAll('[data-eye]').forEach(b=>b.onclick=()=>{const s=cur();if(s)s.opt.eye=b.dataset.eye;else open({eye:b.dataset.eye});});
  e.querySelector('[data-ps1]').onclick=()=>{OPT.ps1=!OPT.ps1;};};
 const update=App.updatePanel.bind(App);App.updatePanel=function(){if(!(App.top() instanceof Screen)){update();return;}document.getElementById('p-where').textContent='BreadBox X · 3D room';document.getElementById('p-source').textContent=App.top().source;};
 // ------------------------------------------------------------------ BBH1 bake (device export of the hands)
 // Runs the same world simulation as the films and samples every 1/30 s: the camera, the hands' bone
 // matrices (5 per hand: palm/wrist, thumb, back, fingers, sleeve), the held object's matrices (case root,
 // lid / flap / cartridge, or the QR card) and the fade, all in a clip-local space (SPEC.md "BBH1").
 // tools/breadbox-r10-bbh.py packs the result with the hand mesh into export/hands.bbh.
 // every = sim steps per sample (4 = 30 Hz, the device clips; 2 = 60 Hz); motion: per-frame displacement check
 // Motion bounds: each hand's vertices and the held object's corners in clip
 // space, the largest move between two samples per clip
 let MOTION_TRACE=null,FRAMESTAT=false;
 function bakeAll(every=4,motion=false){
  const rm=room('rustic'),inv=M=>{const R=[M[0],M[4],M[8],0,M[1],M[5],M[9],0,M[2],M[6],M[10],0,0,0,0,1],t=xf(R,[M[12],M[13],M[14]]);R[12]=-t[0];R[13]=-t[1];R[14]=-t[2];return R;};
  const m34=M=>[M[0],M[4],M[8],M[12],M[1],M[5],M[9],M[13],M[2],M[6],M[10],M[14]].map(v=>+v.toFixed(4));
  const glove=OPT.glove,R_=rigOf(rm),out=[],MOTION=[];
  // a hand -> {palm, sleeve (m34, clip space), j (15 x [flex, spread], radians), pen (mm)}
  const handRec=(h,S)=>{if(!h||(h.slide||0)>=318)return null;const M=handMats(rm,h),j=[];for(let k=2;k<RH.NB;k++)j.push(+h.pose[k*2].toFixed(4),+h.pose[k*2+1].toFixed(4));
   const pr=handPen(rm,h,true);return{palm:m34(mul(S,M[1])),sleeve:m34(mul(S,M[0])),j,pen:+pr.max.toFixed(3),pb:pr.bone+'b'+pr.box+'u'+(w_.L.p.toFixed(2))};};
  let w_=null;
  // the posed hand's vertices (clip space), and the corners of a held object
  const handVerts=(h,S)=>{if(!h||(h.slide||0)>=318)return null;const M=handMats(rm,h),Sm=isMitt()?R_.mitt:R_.bare,V=Sm.V,out=new Float32Array(Sm.nv*3);
   for(let i=0;i<Sm.nv;i++){const m=mul(S,M[Sm.slot[i]]),p=xf(m,[V[i*4],V[i*4+1],V[i*4+2]]);out[i*3]=p[0];out[i*3+1]=p[1];out[i*3+2]=p[2];}return out;};
  const objCorners=(k,M,thw)=>{const b={T:thw[0],H:thw[1],W:thw[2]},out=[];
   for(const x of[-b.T/2,b.T/2])for(const y of[0,b.H])for(const z of[-b.W/2,b.W/2])out.push(...xf(M,[x,y,z]));return out;};
  const dpalm=(a,b)=>{if(!a||!b)return 0;const sl=(isMitt()?R_.mitt:R_.bare).slot;let m=0;for(let i=0;i<a.length;i+=3)if(sl[i/3]===1)m=Math.max(m,Math.hypot(a[i]-b[i],a[i+1]-b[i+1],a[i+2]-b[i+2]));return m;};
  const wslot=(a,b)=>{if(!a||!b)return -1;const sl=(isMitt()?R_.mitt:R_.bare).slot;let m=0,w=-1;for(let i=0;i<a.length;i+=3){const d=Math.hypot(a[i]-b[i],a[i+1]-b[i+1],a[i+2]-b[i+2]);if(d>m){m=d;w=sl[i/3];}}return w;};
  const pcen=a=>{if(!a)return[];const sl=(isMitt()?R_.mitt:R_.bare).slot;const c=[0,0,0];let n=0;for(let i=0;i<a.length;i+=3)if(sl[i/3]===1){c[0]+=a[i];c[1]+=a[i+1];c[2]+=a[i+2];n++;}return c.map(v=>+(v/n).toFixed(1));};
  const dmax=(a,b)=>{if(!a||!b)return 0;let m=0;for(let i=0;i<a.length;i+=3)m=Math.max(m,Math.hypot(a[i]-b[i],a[i+1]-b[i+1],a[i+2]-b[i+2]));return m;};
  // how the held case sits in the top screen (the clip's camera = the device's): its centre x, how far
  // inside the screen it stays, how much of the face toward the camera the hands cover (24 x 24 samples)
  const frameStat=(cam,MbS,ref,hands)=>{const b={T:ref[0],H:ref[1],W:ref[2]},P=p=>cam.proj(...p),C=[];
   for(const x of[-b.T/2,b.T/2])for(const y of[0,b.H])for(const z of[-b.W/2,b.W/2])C.push(P(xf(MbS,[x,y,z])));
   const x0=Math.min(...C.map(c=>c[0])),x1=Math.max(...C.map(c=>c[0])),y0=Math.min(...C.map(c=>c[1])),y1=Math.max(...C.map(c=>c[1]));
   // the face toward the camera: of the six, the one whose centre is nearest
   const faces=[[[1,0,0],b.T/2],[[-1,0,0],b.T/2],[[0,0,1],b.W/2],[[0,0,-1],b.W/2]];let best=null;
   for(const[n,o]of faces){const cw=xf(MbS,[n[0]*o,b.H/2,n[2]*o]),nw=nrm(xf(MbS,[n[0]*(o+1),b.H/2,n[2]*(o+1)]).map((v,i)=>v-cw[i])),tc=nrm(cam.pos.map((v,i)=>v-cw[i])),sc=dot(nw,tc);
    if(!best||sc>best.sc)best={n,o,sc};}   // the face turned most toward the camera
   const tris=[];for(const h of hands){if(!h||(h.slide||0)>=318)continue;const M=handMats(rm,h),Sm=isMitt()?R_.mitt:R_.bare,V=Sm.V,Q=[];
    for(let i=0;i<Sm.nv;i++)Q.push(P(xf(M[Sm.slot[i]],[V[i*4],V[i*4+1],V[i*4+2]])));
    for(const ids of Sm.S)if(ids.length===3)tris.push([Q[ids[0]],Q[ids[1]],Q[ids[2]]]);}
   let hit=0,n=0;const N=24,sgn=(p,a,b_)=>(p[0]-b_[0])*(a[1]-b_[1])-(a[0]-b_[0])*(p[1]-b_[1]);
   for(let i=0;i<N;i++)for(let j=0;j<N;j++){const u=(i+.5)/N,v=(j+.5)/N,fn=best.n;
    const lp=fn[0]?[fn[0]*best.o,v*b.H,(u-.5)*b.W]:[(u-.5)*b.T,v*b.H,fn[2]*best.o],q=P(xf(MbS,lp));n++;
    for(const[a,b_,c]of tris){const d1=sgn(q,a,b_),d2=sgn(q,b_,c),d3=sgn(q,c,a);if((d1<0||d2<0||d3<0)&&(d1>0||d2>0||d3>0))continue;
     if((a[2]+b_[2]+c[2])/3<q[2]){hit++;break;}}}
   return{cx:+((x0+x1)/2).toFixed(1),cy:+((y0+y1)/2).toFixed(1),m:+Math.min(x0,400-x1,y0,240-y1).toFixed(1),cov:+(hit/n).toFixed(3),face:best.n.join('')};};
  function frame(w,cam,S){w_=w;const u_=w.L.p;
   const p=curPose(w),f={cam:[...xf(S,p.pos),...xf(S,p.look),p.F].map(v=>+v.toFixed(2)),fade:+fadeAlpha(w).toFixed(4),R:null,L:null,obj:{}};
   const L=w.L;let Rh=null,Lh=null;
   if(w.mode==='lib'&&L.p>.002){
    const PB=pulledBox(rm,w,cam),sys=SYS[PB.g.sys].prop,ref=sys==='ps1'?{T:14,H:125,W:142}:{T:27,H:137,W:120},Sc=[PB.b.T/ref.T,PB.b.H/ref.H,PB.b.W/ref.W];
    let MbS=mul(PB.M,[Sc[0],0,0,0,0,Sc[1],0,0,0,0,Sc[2],0,0,0,0,1]),op=null;
    if(w.open){op=openPose(rm,w,PB,ref,openE(openT(w)));MbS=op.MbS;Rh=op.R;Lh=op.L;}else{Rh=holdHand(rm,w,PB,cam);Lh=null;}
    // how far the flying case reaches into the neighbours / the boards while it is still near the cubby (mm; 0 = clear)
    if(motion&&!w.open&&w.L.p<.999){const SA=shelfAround(rm,LIBROWS[w.L.row],PB.g,w.L,TIP),o=SA.S0.slice(12,15),b=PB.b;let dm=0;
     for(const fx of[-1,0,1])for(const fy of[0,.5,1])for(const fz of[-1,0,1]){const p=xf(PB.M,[fx*b.T/2,fy*b.H,fz*b.W/2]),c=[p[0]-o[0],p[1]-o[1],p[2]-o[2]];
      for(const[lo,hi]of SA.local){let dp=1e9;for(let i=0;i<3;i++)dp=Math.min(dp,c[i]-lo[i],hi[i]-c[i]);if(dp>0)dm=Math.max(dm,dp);}}
     f.cc=+dm.toFixed(2);}
    const pp=props().parts;f.obj.case=MbS;f.ref=[ref.T,ref.H,ref.W];
    if(sys==='ps1')f.obj.lid=mul(MbS,hingeAt(Sc,pp.ps1_lid.pivot,rotY(op?op.lidA:0)));
    else{f.obj.cart=mul(MbS,op?op.cart:I4);f.obj.flap=mul(MbS,hingeAt(Sc,pp.gba_flap.pivot,rotZ(op?op.flapA:0)));}
   }
   if(w.share.p>.002){const cf=cardFrame(rm,w,cam);f.obj.card=cf.Mc;f.cardTHW=[cf.b.T,+cf.b.H.toFixed(1),+cf.b.W.toFixed(1)];Rh=cardSpec(w,cam);}
   if(w.mode==='at'&&w.place==='settings'&&w.pick.k>.01){[Rh,Lh]=pickerSpecs(w,cam);}
   if(motion&&FRAMESTAT&&f.obj.case&&!w.open&&w.L.p>.995&&w.L.pTo===1)f.fs=frameStat(cam,f.obj.case,f.ref,[Rh,Lh]);
   if(motion){f.u=w.open?openT(w):w.share.p>.002?w.share.p:w.pick.k>.01?w.pick.k:u_;f.ph=Rh&&Rh.pose?[...(PATHDBG||[]),...[11,12,13,14,15,16].map(j=>+Rh.pose[j*2].toFixed(3))]:'';PATHDBG=null;}
   if(motion)f.mv={R:handVerts(Rh,S),L:handVerts(Lh,S),obj:Object.fromEntries(Object.entries(f.obj).map(([k,M])=>[k,objCorners(k,mul(S,M),f.cardTHW||f.ref)]))};
   f.R=handRec(Rh,S);f.L=handRec(Lh,S);f.obj=Object.fromEntries(Object.entries(f.obj).map(([k,M])=>[k,m34(mul(S,M))]));
   return f;
  }
  function run(setup,keys,dur,space){
   const w=world(rm,'continue');w.sim=true;setup(w);const fr=[],ev={};let k=0,n=0,S=null,lastP=null;
   while(w.t<dur){
    while(k<keys.length&&keys[k][0]<=w.t+1e-9){act(w,keys[k][1]);k++;}
    step(w,DT);n++;
    if(w.L.pTo===1&&w.L.pT0!==lastP){lastP=w.L.pT0;(ev.pull=ev.pull||[]).push(w.L.pT0);}
    if(n%every===0){const cam=useCamOf(curPose(w),0);
     if(!S)S=space(w,cam);if(S)fr.push({t:+w.t.toFixed(4),...frame(w,cam,S)});}
   }
   return{fr,ev,S};
  }
  const slice=(R,name,t0,t1,o)=>{const f=R.fr.filter(q=>q.t>=t0-1e-6&&q.t<t1+1e-6),cf=f.find(q=>q.cardTHW);out.push(Object.assign({name,frames:f},o,cf?{boxTHW:cf.cardTHW}:{}));
   if(motion){const st={name,hand:0,handAt:-1,obj:0,objAt:-1};for(let i=1;i<f.length;i++){const a=f[i-1].mv,b=f[i].mv;
     for(const h of['R','L']){const d=dmax(a[h],b[h]);if(d>st.hand){st.hand=d;st.handAt=+f[i].t.toFixed(3);st.handWho=h;}}
     for(const k in b.obj){const d=dmax(a.obj[k],b.obj[k]);if(d>st.obj){st.obj=d;st.objAt=+f[i].t.toFixed(3);st.objWho=k;}}
     if(MOTION_TRACE===name)(st.trace=st.trace||[]).push([+f[i].t.toFixed(3),+f[i].u.toFixed(4),+dmax(a.R,b.R).toFixed(1),+dmax(a.L,b.L).toFixed(1),+(b.obj.case?dmax(a.obj.case,b.obj.case):0).toFixed(1),+dpalm(a.R,b.R).toFixed(1),wslot(a.R,b.R),f[i].ph,f[i].cc]);}
    // the case's sideways range (slot x of its centre), and for a loop the seam: last frame vs first
    // (position) and the last step vs the first step (velocity), over every hand vertex and object corner
    const cx=f.filter(q=>q.mv.obj.case).map(q=>{const c=q.mv.obj.case;let x=0;for(let i=0;i<c.length;i+=3)x+=c[i];return x/(c.length/3);});
    st.lat=cx.length?+(Math.max(...cx)-Math.min(...cx)).toFixed(2):0;
    st.cc=Math.max(0,...f.filter(q=>q.cc!=null).map(q=>q.cc));   // the case's deepest reach into the shelf's neighbours (mm)
    if(o&&o.loop&&f.length>2){const A=f[0].mv,B=f[1].mv,Y=f[f.length-2].mv,Z=f[f.length-1].mv;let sp=0,sv=0;
     const acc=(a,b,y,z)=>{if(!a||!z)return;for(let i=0;i<a.length;i++){sp=Math.max(sp,Math.abs(z[i]-a[i]));sv=Math.max(sv,Math.abs((z[i]-y[i])-(b[i]-a[i])));}};
     acc(A.R,B.R,Y.R,Z.R);acc(A.L,B.L,Y.L,Z.L);for(const k in A.obj)acc(A.obj[k],B.obj[k],Y.obj[k],Z.obj[k]);st.seam=+sp.toFixed(3);st.seamV=+sv.toFixed(3);}
    const fs=f.filter(q=>q.fs).map(q=>q.fs);
    if(fs.length)st.frame={dx:+Math.max(...fs.map(q=>Math.abs(q.cx-200))).toFixed(1),cy:+(fs.reduce((a,q)=>a+q.cy,0)/fs.length).toFixed(1),m:Math.min(...fs.map(q=>q.m)),cov:Math.max(...fs.filter(q=>q.face==='100').map(q=>q.cov),0),last:f[f.length-1].fs||null};
    st.hand=+st.hand.toFixed(1);st.obj=+st.obj.toFixed(1);st.t0=+(f[0]?f[0].t:0).toFixed(3);MOTION.push(st);}};
  for(const[gi,suf]of[[0,''],[5,'_m']]){
   OPT.glove=gi;
   // library clips per box class: take out, idle, the four turns, put back, game start
   // one clip set per box class: ps1 (NA / JP jewel, 10.5 mm), ps1p (PAL, 17), ps1d (2+ discs, 25), gba (23)
   for(const cls of['ps1','ps1p','ps1d','gba']){
    const sid=cls==='gba'?'gba':'ps1',idx=LIBROWS.findIndex(r=>r.sid===sid),rowC=LIBROWS[idx],gC=rowC.games[rowC.sel],b0=gC.b;
    gC.b={...BOXOF({sys:sid,title:''}),T:{ps1:10.5,ps1p:17,ps1d:25,gba:23}[cls]};
    const setup=w=>{w.mode='lib';w.place='library';w.focus='library';w.t0=-5;const L=w.L;L.row=L.hsel=idx;L.hy=LIBROWS[idx].base;L.camx=LIBROWS[idx].cx;};
    // the slot space: the selected box standing in the shelf (identity rotation), fixed at the take-out
    const space=w=>{if(!(w.L.pTo===1&&w.L.p>0))return null;const row=LIBROWS[w.L.row],g=row.games[row.sel];return T4(...[-boxX(row,g,secOff(row,w.L.lcam)),-row.base,-(4+g.b.W/2)]);};
    const len=OPEN[sid].len+BLACK_T;
    // A takes the box at T0; the hold clip is one IDLE_P period from the pick's end (seamless: its last
    // frame is its first); the turns, the put-back and the game start begin on that phase (they start from it)
    // the hold starts at 4.7 s as before (so the idle's phase, and with it every hold / turn / open clip, is
    // byte for byte the old one); the shorter take-out starts that much later
    const TH=4.7,T0=TH-PULL_T,TS=TH+IDLE_P,TB=TH+3*IDLE_P,TO=TB+PUT_T+.35+PULL_T+.45;
    const TOP=TH+Math.ceil((TO-TH)/IDLE_P)*IDLE_P;
    const R=run(setup,[[.3,'A'],[T0,'A'],[TS,'R'],[TS+1.2,'R'],[TS+2.4,'L'],[TS+3.6,'L'],[TB,'B'],[TB+PUT_T+.35,'A'],[TOP,'A']],TOP+len+.1,space);
    const p0=R.ev.pull[0],row=LIBROWS[idx],g=row.games[row.sel],o={space:'slot',box:cls,boxTHW:[g.b.T,g.b.H,g.b.W]};
    slice(R,'pick_'+cls+suf,p0,p0+PULL_T,o);slice(R,'hold_'+cls+suf,TH,TH+IDLE_P,Object.assign({loop:1},o));
    // the four turns (R twice: front -> spine -> back; L twice: back -> spine -> front)
    for(const[nm,t0]of[['turn_r1_',TS],['turn_r2_',TS+1.2],['turn_l1_',TS+2.4],['turn_l2_',TS+3.6]])slice(R,nm+cls+suf,t0,t0+TURN_T,o);
    slice(R,'putback_'+cls+suf,TB,TB+PUT_T,o);slice(R,'open_'+cls+suf,TOP,TOP+len,o);
   gC.b=b0;
   }
   // the QR card (stand space: the card's pivot, identity rotation)
   {const R=run(w=>{w.mode='at';w.place='share';w.focus='share';w.t0=-5;},[[.3,'A'],[.45+CARD_IN+IDLE_P,'B']],.45+CARD_IN+IDLE_P+CARD_OUT+.1,w=>{const p=rm.parts.a_card.pivot;return T4(-p[0],-p[1],-p[2]);});
    const o={space:'card'};slice(R,'card_take'+suf,.3,.3+CARD_IN,o);slice(R,'card_hold'+suf,.4+CARD_IN,.4+CARD_IN+IDLE_P,Object.assign({loop:1},o));slice(R,'card_return'+suf,.45+CARD_IN+IDLE_P,.45+CARD_IN+IDLE_P+CARD_OUT-.05,o);}   // the hold is one IDLE_P
   // the customize screen (camera space: the scope camera, x right, y up, z forward)
   {const R=run(w=>{w.mode='at';w.place='settings';w.focus='settings';w.t0=-5;w.tuneSel=w.tune=HAND_CH;},[[.3,'A'],[6.0,'B']],7.4,
     (w,cam)=>inv([cam.R[0],cam.R[1],cam.R[2],0,cam.U[0],cam.U[1],cam.U[2],0,cam.Fw[0],cam.Fw[1],cam.Fw[2],0,cam.pos[0],cam.pos[1],cam.pos[2],1]));
    const o={space:'camera'};slice(R,'picker_in'+suf,.3,1.6,o);slice(R,'picker_idle'+suf,1.9,1.9+IDLE_P,Object.assign({loop:1},o));slice(R,'picker_out'+suf,6.0,7.3,o);}
  }
  OPT.glove=glove;
  if(motion)return MOTION;
  const pen=out.map(c=>{let m=0,at=-1,wh='';c.frames.forEach((f,i)=>{for(const[n,h]of[['R',f.R],['L',f.L]])if(h&&h.pen>m){m=h.pen;at=i;wh=n+h.pb;}});return[c.name,+m.toFixed(2),at,c.frames.length,wh];});
  return{fps:30,pen,clips:out.map(c=>({name:c.name,space:c.space,box:c.box||'',boxTHW:c.boxTHW||[0,0,0],loop:c.loop||0,
    frames:c.frames.map(f=>({cam:f.cam,fade:f.fade,R:f.R,L:f.L,obj:f.obj}))})),
   rig:{bones:Array.from({length:RH.NB-2},(_,i)=>{const b=R_.B[i+2];return{head:b.h,flex:b.f,spread:b.s,parent:b.p};}),sleevePivot:R_.sleevePivot,hand:rm.A.hand,hand2:rm.A.hand2,hand3:rm.A.hand3}};
 }
 return{Screen,open,SCRIPTS,ROOMS,room,parse,OPT,LIBROWS,bakeAll};
})();
