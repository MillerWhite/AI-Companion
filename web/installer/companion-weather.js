/* Atmospheric weather layer for the companion face.
 * Lightweight canvas particles; eye rendering stays in front and fully visible.
 * Demo modes only: no location lookup or claim of live local weather.
 */
(function(root){
  'use strict';
  const TYPES=['clear','cloudy','rain','storm','snow'];
  const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
  function createWeatherScene(canvas){
    if(!canvas||!canvas.getContext)return {setMode(){},destroy(){}};
    const ctx=canvas.getContext('2d');
    if(!ctx)return {setMode(){},destroy(){}};
    let w=1,h=1,dpr=1,mode='clear',active='clear',auto=false,alive=true,last=0,nextChange=0,flash=0,wind=.15;
    let rain=[],snow=[],stars=[];
    const rand=(a,b)=>a+Math.random()*(b-a);
    function resize(){
      const r=canvas.getBoundingClientRect();w=Math.max(1,r.width||canvas.clientWidth||220);h=Math.max(1,r.height||canvas.clientHeight||220);
      dpr=Math.min(2,root.devicePixelRatio||1);canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);
      rain=Array.from({length:72},()=>({x:rand(-w,w),y:rand(-h,h),len:rand(7,18),speed:rand(130,260),alpha:rand(.12,.42)}));
      snow=Array.from({length:38},()=>({x:rand(0,w),y:rand(0,h),r:rand(.7,2.4),speed:rand(12,38),phase:rand(0,6.28),alpha:rand(.25,.8)}));
      stars=Array.from({length:26},()=>({x:rand(0,w),y:rand(0,h*.65),r:rand(.3,1.1),phase:rand(0,6.28)}));
    }
    function setMode(next){
      mode=TYPES.includes(next)?next:'clear';auto=next==='auto';
      if(!auto)active=mode;
      nextChange=performance.now()+rand(35000,70000);
    }
    function cloud(x,y,s,alpha,dark){
      ctx.save();ctx.globalAlpha=alpha;
      const g=ctx.createLinearGradient(0,y-s*.3,0,y+s*.5);
      g.addColorStop(0,dark?'#8997a6':'#e5f1fb');g.addColorStop(1,dark?'#273747':'#718da4');
      ctx.fillStyle=g;ctx.shadowColor=dark?'#8fa9c455':'#b9e5ff55';ctx.shadowBlur=12*s;
      ctx.beginPath();ctx.ellipse(x,y,s*.55,s*.23,0,0,Math.PI*2);ctx.ellipse(x-s*.25,y-s*.12,s*.28,s*.27,0,0,Math.PI*2);ctx.ellipse(x+s*.12,y-s*.2,s*.34,s*.33,0,0,Math.PI*2);ctx.ellipse(x+s*.38,y-s*.07,s*.25,s*.22,0,0,Math.PI*2);ctx.fill();ctx.restore();
    }
    function drawSky(t){
      const g=ctx.createRadialGradient(w*.5,h*.12,2,w*.5,h*.5,w*.8);
      g.addColorStop(0,active==='storm'?'#263344':active==='rain'?'#243746':active==='snow'?'#253746':'#152638');
      g.addColorStop(1,'rgba(0,0,0,0)');ctx.fillStyle=g;ctx.fillRect(0,0,w,h);
      if(active==='clear'||active==='cloudy'){
        const x=w*.72,y=h*.25,r=Math.min(w,h)*.095;
        const halo=ctx.createRadialGradient(x,y,1,x,y,r*3);halo.addColorStop(0,'#f4e6b866');halo.addColorStop(1,'#f4e6b800');ctx.fillStyle=halo;ctx.fillRect(x-r*3,y-r*3,r*6,r*6);
        ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.fillStyle=active==='clear'?'#f3e5b8':'#c8d8e5';ctx.fill();
        if(active==='cloudy')cloud(w*.45,h*.25,w*.48,.68,true);
      }else{
        cloud(w*.48,h*.2,w*.78,active==='storm'?.88:.72,true);
        cloud(w*.82,h*.27,w*.48,.38,true);
      }
      if(active==='clear'&&h>100){for(const s of stars){const a=.1+.22*(.5+.5*Math.sin(t*.001+s.phase));ctx.globalAlpha=a;ctx.fillStyle='#d7edff';ctx.beginPath();ctx.arc(s.x,s.y,s.r,0,Math.PI*2);ctx.fill()}ctx.globalAlpha=1;}
    }
    function drawRain(dt){
      ctx.lineWidth=1.15;ctx.lineCap='round';
      for(const p of rain){p.y+=p.speed*dt;p.x+=wind*p.speed*dt*.8;if(p.y>h+12){p.y=rand(-30,-5);p.x=rand(-10,w+10)}
        ctx.strokeStyle='rgba(174,220,245,'+p.alpha+')';ctx.beginPath();ctx.moveTo(p.x,p.y);ctx.lineTo(p.x-wind*4,p.y-p.len);ctx.stroke();}
      if(active==='storm'&&Math.random()<.0018){flash=rand(90,180)}
      if(flash>0){ctx.fillStyle='rgba(215,235,255,'+Math.min(.22,flash/650)+')';ctx.fillRect(0,0,w,h);flash-=dt*1000;
        ctx.strokeStyle='rgba(232,246,255,.82)';ctx.lineWidth=2;ctx.beginPath();let x=rand(w*.25,w*.8),y=h*.17;ctx.moveTo(x,y);for(let i=0;i<4;i++){x+=rand(-12,12);y+=h*.095;ctx.lineTo(x,y)}ctx.stroke();}
      ctx.fillStyle='rgba(170,215,240,.15)';ctx.fillRect(0,h*.92,w,h*.08);
    }
    function drawSnow(dt,t){
      for(const p of snow){p.y+=p.speed*dt;p.x+=(wind*12+Math.sin(t*.001+p.phase)*9)*dt;if(p.y>h+5){p.y=-5;p.x=rand(0,w)}if(p.x<-5)p.x=w+5;if(p.x>w+5)p.x=-5;
        ctx.globalAlpha=p.alpha;ctx.fillStyle='#e9f6ff';ctx.shadowColor='#b8e8ff';ctx.shadowBlur=p.r*4;ctx.beginPath();ctx.arc(p.x,p.y,p.r,0,Math.PI*2);ctx.fill();}
      ctx.globalAlpha=1;ctx.shadowBlur=0;
    }
    function frame(now){
      if(!alive)return;const dt=last?clamp((now-last)/1000,0,.05):.016;last=now;
      if(auto&&now>=nextChange){const options=TYPES;let n=options[Math.floor(Math.random()*options.length)];if(n===active)n='cloudy';active=n;nextChange=now+rand(35000,70000);}
      ctx.clearRect(0,0,w,h);drawSky(now);
      wind+=(rand(-.3,.3)-wind)*dt*.08;
      if(active==='rain'||active==='storm')drawRain(dt);
      if(active==='snow')drawSnow(dt,now);
      root.requestAnimationFrame(frame);
    }
    resize();setMode('clear');
    if(root.ResizeObserver){const ro=new ResizeObserver(resize);ro.observe(canvas);canvas.__weatherObserver=ro;}
    root.requestAnimationFrame(frame);
    return {setMode, getMode:()=>auto?'auto':active,destroy(){alive=false;if(canvas.__weatherObserver)canvas.__weatherObserver.disconnect();}};
  }
  const api={createWeatherScene,TYPES};
  root.AICompanionWeather=api;
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof globalThis!=='undefined'?globalThis:this);
