/* Weather FX for AI Companion.
 * Rain cloud and falling-drop timing/trajectory are ported from Pip-KeyPet ui/pip.c
 * and ui/doodle.c by David Novikov (MIT); see third_party/Pip-KeyPet-LICENSE.txt.
 * Pip-KeyPet itself has a rain-cloud mood effect, not a full weather/forecast engine.
 */
(function(root){
  'use strict';
  const TYPES=['clear','cloudy','rain','storm','snow'];
  const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
  const rand=(a,b)=>a+Math.random()*(b-a);
  function createWeatherScene(canvas){
    if(!canvas||!canvas.getContext)return{setMode(){},destroy(){}};
    const ctx=canvas.getContext('2d');if(!ctx)return{setMode(){},destroy(){}};
    let w=1,h=1,dpr=1,mode='clear',active='clear',auto=false,alive=true,last=0,nextChange=0,spawn=0,flash=0;
    let drops=[],flakes=[],observer=null,raf=0;
    function resize(){
      const r=canvas.getBoundingClientRect();w=Math.max(1,r.width||220);h=Math.max(1,r.height||220);
      dpr=Math.min(2,root.devicePixelRatio||1);canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);
      ctx.setTransform(dpr,0,0,dpr,0,0);drops=[];flakes=[];
    }
    function setMode(next){mode=TYPES.includes(next)?next:'clear';auto=next==='auto';if(!auto)active=mode;nextChange=performance.now()+rand(35000,70000);}
    function scale(){return Math.min(w,h)/466;}
    function cloud(t){
      const k=scale(),cw=170*k,ch=80*k,cx=w*.5-cw*.5+Math.sin(t*.0007)*10*k,cy=26*k;
      const puffs=[[40,50,24],[72,34,30],[108,38,28],[134,52,22],[86,56,22]];
      ctx.save();ctx.translate(cx,cy);ctx.scale(k,k);
      const g=ctx.createLinearGradient(0,8,0,76);g.addColorStop(0,'#747e88');g.addColorStop(.5,'#5c6670');g.addColorStop(1,'#424c56');
      ctx.fillStyle=g;ctx.shadowColor='rgba(165,190,210,.16)';ctx.shadowBlur=12;
      ctx.beginPath();
      for(const [x,y,r] of puffs){ctx.moveTo(x+r,y);ctx.arc(x,y,r,0,Math.PI*2);}
      ctx.fill();ctx.shadowBlur=0;
      const hi=ctx.createLinearGradient(0,12,0,52);hi.addColorStop(0,'rgba(235,244,250,.22)');hi.addColorStop(1,'rgba(235,244,250,0)');
      ctx.fillStyle=hi;ctx.beginPath();ctx.ellipse(86,36,59,20,0,0,Math.PI*2);ctx.fill();
      ctx.restore();
      return{x:cx,y:cy,k};
    }
    function spawnDrop(c){
      const k=c.k;
      drops.push({x:c.x+rand(30,140)*k,y:c.y+92*k,vx:-25*k,vy:rand(300,360)*k,life:1});
    }
    function drawRain(dt,c){
      spawn-=dt;
      if(spawn<=0){spawn=rand(.03,.08);if(drops.length<44)spawnDrop(c);}
      ctx.lineCap='round';
      for(let i=drops.length-1;i>=0;i--){
        const p=drops[i];p.x+=p.vx*dt;p.y+=p.vy*dt;p.life-=dt*.24;
        ctx.strokeStyle='rgba(174,210,230,'+clamp(p.life*.62,.12,.52)+')';
        ctx.lineWidth=Math.max(.65,1.25*c.k);ctx.beginPath();ctx.moveTo(p.x,p.y);ctx.lineTo(p.x-1.2*c.k,p.y-7*c.k);ctx.stroke();
        if(p.y>h+12||p.life<=0)drops.splice(i,1);
      }
    }
    function drawSnow(dt,t){
      const k=scale();if(flakes.length<30&&Math.random()<dt*8)flakes.push({x:rand(0,w),y:-4,r:rand(1,2.2)*k,v:rand(12,28)*k,phase:rand(0,6.28)});
      for(let i=flakes.length-1;i>=0;i--){const p=flakes[i];p.y+=p.v*dt;p.x+=Math.sin(t*.001+p.phase)*12*k*dt;ctx.fillStyle='rgba(225,240,250,.62)';ctx.beginPath();ctx.arc(p.x,p.y,p.r,0,Math.PI*2);ctx.fill();if(p.y>h+5)flakes.splice(i,1);}
    }
    function frame(now){
      if(!alive)return;const dt=last?clamp((now-last)/1000,0,.05):.016;last=now;
      if(auto&&now>=nextChange){const opts=TYPES;active=opts[Math.floor(Math.random()*opts.length)];nextChange=now+rand(35000,70000);}
      ctx.clearRect(0,0,w,h);
      if(active==='clear'){
        const k=scale(),x=w*.72,y=h*.22,r=13*k,g=ctx.createRadialGradient(x,y,1,x,y,r*3);
        g.addColorStop(0,'rgba(239,224,185,.22)');g.addColorStop(1,'rgba(239,224,185,0)');
        ctx.fillStyle=g;ctx.fillRect(x-r*3,y-r*3,r*6,r*6);ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.fillStyle='rgba(239,224,185,.55)';ctx.fill();
      }else{
        const c=cloud(now);
        if(active==='rain'||active==='storm')drawRain(dt,c);
        if(active==='snow')drawSnow(dt,now);
        if(active==='storm'){
          if(Math.random()<.0012)flash=rand(70,130);
          if(flash>0){ctx.fillStyle='rgba(220,235,248,'+Math.min(.12,flash/1100)+')';ctx.fillRect(0,0,w,h);flash-=dt*1000;}
        }
      }
      raf=root.requestAnimationFrame(frame);
    }
    resize();setMode('clear');
    if(root.ResizeObserver){observer=new ResizeObserver(resize);observer.observe(canvas);}
    raf=root.requestAnimationFrame(frame);
    return{setMode,getMode:()=>auto?'auto':active,destroy(){alive=false;if(observer)observer.disconnect();if(raf)root.cancelAnimationFrame(raf);}};
  }
  const api={createWeatherScene,TYPES};root.AICompanionWeather=api;
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof globalThis!=='undefined'?globalThis:this);
