/* Shared, deterministic-in-tests behavior engine for browser previews.
 * Firmware remains the on-device authority; this module mirrors the same event-driven rules.
 * No care decay, guilt loops, notification spam, or network dependency.
 */
(function(root){
  'use strict';
  const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
  const MODES=['neutral','happy','curious','sleep','sad','love','excited','surprised','shy','angry'];
  const DEFAULTS={temper:'balanced',initiative:'medium',quirk:'curious',weather:'clear'};
  function createCompanionRuntime(options={}){
    let profile={...DEFAULTS,...(options.profile||{})};
    let mood='neutral', moodUntil=0, lastInitiative=0, lastBlink=0, nextBlink=1800;
    let lastGaze=0, nextGaze=2400, gaze={x:0,y:0}, target={x:0,y:0};
    let seed=(options.seed==null?0x6d2b79f5:options.seed)>>>0;
    let lastTick=0, interactionCount=0, trust=25, curiosity=38, calmness=35;
    const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return (seed>>>0)/4294967296;};
    const weighted=(items)=>{let sum=items.reduce((n,x)=>n+x[1],0),r=random()*sum;for(const item of items){r-=item[1];if(r<=0)return item[0];}return items[0][0];};
    function setProfile(next={}){profile={...profile,...next};}
    function setMood(next,now=Date.now(),duration=0){if(!MODES.includes(next))return false;mood=next;moodUntil=duration>0?now+duration:0;return true;}
    function event(kind,now=Date.now()){
      interactionCount++;
      if(kind==='touch'||kind==='pet'||kind==='long_pet'){trust=clamp(trust+(kind==='long_pet'?3:1.5),0,100);calmness=clamp(calmness+1,0,100);setMood(kind==='long_pet'?'love':'happy',now,2600);}
      else if(kind==='call'||kind==='name'){curiosity=clamp(curiosity+2,0,100);setMood('curious',now,2300);}
      else if(kind==='shake'){setMood('surprised',now,1800);}
      else if(kind==='drop'||kind==='impact'){setMood('scared',now,1400);}
      else if(kind==='comfort'){setMood('love',now,3000);}
      else if(kind==='rest'){setMood('sleep',now,0);}
      else if(kind==='wake'){setMood('curious',now,1800);}
      return snapshot();
    }
    function chooseInitiative(now,hour){
      const cooldown=profile.initiative==='high'?90000:profile.initiative==='low'?300000:180000;
      if(now-lastInitiative<cooldown||mood==='sleep')return null;
      lastInitiative=now;
      const chance=profile.initiative==='high'?.42:profile.initiative==='low'?.12:.25;
      if(random()>chance)return null;
      const weights=[['neutral',22],['curious',24],['happy',15],['shy',8],['love',trust>60?13:4],['excited',profile.temper==='energetic'?18:7],['sleep',profile.quirk==='sleepy'&&(hour>=22||hour<7)?12:1],['surprised',4]];
      if(profile.temper==='calm'){weights[0][1]+=18;weights[5][1]=2;}
      if(profile.temper==='shy'){weights[3][1]+=18;weights[5][1]=2;}
      if(profile.temper==='dreamy'){weights[1][1]+=10;weights[0][1]+=8;}
      if(profile.temper==='energetic'){weights[5][1]+=12;weights[2][1]+=8;}
      if(profile.quirk==='curious')weights[1][1]+=12;
      if(profile.quirk==='dramatic')weights[7][1]+=8;
      if(profile.weather==='rain'||profile.weather==='cloudy')weights[0][1]+=3;
      if(profile.weather==='sunny')weights[2][1]+=3;
      const chosen=weighted(weights);
      return chosen===mood?'neutral':chosen;
    }
    function snapshot(){return {mood,trust,curiosity,calmness,interactionCount,gaze:{...gaze},weather:profile.weather,personality:{...profile}};}
    function tick(now=Date.now(),context={}){
      if(!lastTick)lastTick=now;
      const dt=clamp((now-lastTick)/1000,0,.1);lastTick=now;
      if(moodUntil&&now>=moodUntil){mood='neutral';moodUntil=0;}
      if(context.weather)setProfile({weather:context.weather});
      if(context.tilt){target.x=clamp(context.tilt.x,-1,1);target.y=clamp(context.tilt.y,-1,1);}
      if(now>=nextGaze){target.x=(random()-.5)*.75;target.y=(random()-.5)*.5;nextGaze=now+2200+random()*4200;lastGaze=now;}
      const blend=1-Math.exp(-dt*4.2);gaze.x+=(target.x-gaze.x)*blend;gaze.y+=(target.y-gaze.y)*blend;
      let blink=false;if(now>=nextBlink&&mood!=='sleep'){blink=true;lastBlink=now;nextBlink=now+2200+random()*4200;}
      let initiative=null;if(!context.userActive&&now>=nextInitiativeAt()){initiative=chooseInitiative(now,context.hour==null?12:context.hour);}
      return {mood,gaze:{...gaze},blink,initiative,weather:profile.weather};
    }
    let initiativeGate=0;
    function nextInitiativeAt(){if(!initiativeGate)initiativeGate=Date.now()+15000;return initiativeGate;}
    function resetInitiativeGate(now=Date.now()){initiativeGate=now;}
    return {setProfile,setMood,event,tick,snapshot,resetInitiativeGate};
  }
  root.AICompanionRuntime={createCompanionRuntime,MODES};
  if(typeof module!=='undefined'&&module.exports)module.exports=root.AICompanionRuntime;
})(typeof globalThis!=='undefined'?globalThis:this);
