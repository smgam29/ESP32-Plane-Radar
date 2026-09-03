// Run with: node test/label_picker_test.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const page = fs.readFileSync('src/services/web_portal.cpp', 'utf8');
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '', checked: false, disabled: false, textContent: '', listeners: {},
    addEventListener(name, callback) { this.listeners[name] = callback; }
  });
  return elements.get(id);
}
const boxes = Array.from({length: 10}, (_, i) => {
  const box = element('label-' + i);
  box.value = String(1 << i);
  assert(page.includes('value="' + box.value + '"'));
  return box;
});
const writes = [];
vm.runInNewContext(script, {
  document: { getElementById: element, querySelectorAll: () => boxes },
  URLSearchParams,
  fetch: async (url, options) => {
    if (options && options.method === 'POST') writes.push(options.body.toString());
    return {ok: true, json: async () => url === '/api/status'
      ? {labelMask: 7} : {message: 'Plane labels saved.'}};
  }
});
(async () => {
  await new Promise(setImmediate);
  assert.equal(boxes.filter(b => b.checked).length, 3);
  assert(boxes.slice(3).every(b => b.disabled));
  assert.match(element('label-count').textContent, /deselect one/);
  boxes[1].checked = false;
  boxes[1].listeners.change();
  assert(boxes.every(b => !b.disabled));
  boxes[9].checked = true;
  boxes[9].listeners.change();
  assert(boxes[3].disabled);
  const submit = () => element('labels-form').listeners.submit({preventDefault() {}});
  submit();
  await new Promise(setImmediate);
  assert.deepEqual(writes, ['mask=517']);
  // A programmatic fourth selection is also rejected at submit time.
  boxes[8].checked = true;
  submit();
  assert.equal(writes.length, 1);
  assert.match(element('labels-message').textContent, /at most three/);
  boxes.forEach(b => b.checked = false);
  boxes[0].listeners.change();
  submit();
  await new Promise(setImmediate);
  assert.equal(writes[1], 'mask=0');
  assert.equal(element('save-appearance').disabled, false);
  for (const enabled of [true, false]) {
    element('dim-rings').checked = enabled;
    element('appearance-form').listeners.submit({preventDefault() {}});
    await new Promise(setImmediate);
    assert.equal(writes[writes.length - 1], 'dimRings=' + (enabled ? '1' : '0'));
    assert.equal(element('save-appearance').disabled, false);
  }
  console.log('Label picker UI tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
