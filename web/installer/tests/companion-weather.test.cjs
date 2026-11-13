'use strict';
const assert=require('node:assert/strict');
const weather=require('../companion-weather.js');
assert.deepEqual(weather.TYPES,['clear','cloudy','rain','storm','snow']);
assert.equal(typeof weather.createWeatherScene,'function');
assert.doesNotThrow(()=>weather.createWeatherScene(null).setMode('rain'));
console.log('companion weather runtime tests passed');
