// The WebMCP adapter (src/probe/transport/webmcp.js) against a stand-in page: a fake qtpilot
// bridge and a fake modelContext, so it runs in Node without a browser or a wasm build.
//   node --test tests/web/
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import vm from 'node:vm';

// Values made inside the vm context have that realm's prototypes; compare them as plain data.
const plain = (value) => JSON.parse(JSON.stringify(value));

const source = readFileSync(new URL('../../src/probe/transport/webmcp.js', import.meta.url), 'utf8');

function page({optIn = true, contextOn = 'navigator', search = ''} = {}) {
  const calls = [];
  const tools = new Map();
  const modelContext = {
    registerTool(tool, options) {
      tools.set(tool.name, tool);
      options?.signal?.addEventListener('abort', () => tools.delete(tool.name));
    },
  };
  const globals = {
    location: {search},
    navigator: {},
    document: {},
    qtpilot: {
      call(request) {
        const parsed = JSON.parse(request);
        calls.push(parsed);
        return JSON.stringify({jsonrpc: '2.0', id: parsed.id, result: {method: parsed.method, params: parsed.params}});
      },
    },
    AbortController,
    JSON,
  };
  globals[contextOn].modelContext = modelContext;
  if (optIn) globals.qtpilotWebMcp = true;
  globals.globalThis = globals;
  vm.runInNewContext(source, globals);
  return {globals, calls, tools};
}

test('registers the read and drive tools when the page opts in', () => {
  const {globals, tools} = page();
  const installed = globals.qtpilot.installWebMcp();
  assert.deepEqual([...tools.keys()].sort(), ['qt_click', 'qt_find', 'qt_get_property', 'qt_ping', 'qt_screenshot']);
  assert.deepEqual(plain(installed).sort(), [...tools.keys()].sort());
  for (const tool of tools.values()) {
    assert.equal(typeof tool.description, 'string');
    assert.equal(tool.inputSchema.type, 'object');
  }
});

test('a tool forwards one JSON-RPC request through qtpilot.call and returns its result as text', async () => {
  const {globals, calls, tools} = page();
  globals.qtpilot.installWebMcp();
  const reply = await tools.get('qt_find').execute({objectName: 'okButton', limit: 5});
  assert.equal(calls.at(-1).method, 'qt.objects.search');
  assert.deepEqual(plain(calls.at(-1).params), {objectName: 'okButton', limit: 5});
  assert.deepEqual(JSON.parse(reply.content[0].text), {method: 'qt.objects.search', params: {objectName: 'okButton', limit: 5}});
});

test('invoking methods is offered only when asked for', () => {
  const {globals, tools} = page();
  globals.qtpilot.installWebMcp({allowInvoke: true});
  assert.ok(tools.has('qt_invoke'));
});

test('nothing is registered without the opt-in, or without modelContext', () => {
  const quiet = page({optIn: false});
  assert.deepEqual(plain(quiet.globals.qtpilot.installWebMcp()), []);
  assert.equal(quiet.tools.size, 0);

  const absent = page();
  delete absent.globals.navigator.modelContext;
  assert.deepEqual(plain(absent.globals.qtpilot.installWebMcp()), []);
});

test('the opt-in can come from the address (?qtpilot-webmcp), and document.modelContext is used too', () => {
  const {globals, tools} = page({optIn: false, contextOn: 'document', search: '?qtpilot-webmcp'});
  globals.qtpilot.installWebMcp();
  assert.ok(tools.has('qt_ping'));
});

test('uninstalling removes every tool it registered', () => {
  const {globals, tools} = page();
  globals.qtpilot.installWebMcp();
  globals.qtpilot.uninstallWebMcp();
  assert.equal(tools.size, 0);
});
