const assert=require('node:assert/strict');
const {createCompanionRuntime}=require('../companion-runtime.js');
const r=createCompanionRuntime({seed:7,profile:{initiative:'low',temper:'calm'}});
let s=r.snapshot();
assert.equal(s.mood,'neutral');
r.event('touch',1000);assert.equal(r.snapshot().mood,'happy');assert.ok(r.snapshot().trust>25);
r.event('long_pet',2000);assert.equal(r.snapshot().mood,'love');
r.event('call',3000);assert.equal(r.snapshot().mood,'curious');
r.event('shake',4000);assert.equal(r.snapshot().mood,'surprised');
r.event('rest',5000);assert.equal(r.snapshot().mood,'sleep');
r.setProfile({weather:'rain'});assert.equal(r.snapshot().weather,'rain');
r.setMood('angry',6000,100);assert.equal(r.snapshot().mood,'angry');
assert.equal(r.tick(6200).mood,'neutral');
r.setProfile({initiative:'low'});r.resetInitiativeGate(10000);
for(let t=10000;t<20000;t+=100) { const v=r.tick(t,{userActive:true}); assert.ok(Math.abs(v.gaze.x)<=1); assert.ok(Math.abs(v.gaze.y)<=1); }
console.log('shared companion runtime: all tests passed');
