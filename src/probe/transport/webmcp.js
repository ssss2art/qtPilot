// qtPilot's tools for a browser agent, through WebMCP (modelContext.registerTool).
//
// A WebAssembly build of the probe runs this when it installs globalThis.qtpilot
// (js_bridge.cpp embeds it), and it adds qtpilot.installWebMcp() / qtpilot.uninstallWebMcp().
// installWebMcp registers tools only when the page opts in -- globalThis.qtpilotWebMcp === true
// before the app starts, or ?qtpilot-webmcp in the address -- and only when the browser offers
// modelContext (an experiment in Chrome; navigator.modelContext or document.modelContext).
//
// Each tool sends one JSON-RPC request through qtpilot.call, so it does exactly what the same
// method does over qtPilot's other transports. Invoking arbitrary methods is left out unless asked
// for ({allowInvoke: true}): it is the one tool that can change anything the app can.
(function () {
  'use strict';

  const bridge = globalThis.qtpilot;
  if (!bridge) return;

  let nextId = 1;
  let registration = null;  // the AbortController whose signal unregisters our tools

  function call(method, params) {
    const reply = JSON.parse(bridge.call(JSON.stringify({jsonrpc: '2.0', id: nextId++, method, params})));
    if (reply.error) throw new Error(`${method}: ${reply.error.message || JSON.stringify(reply.error)}`);
    return reply.result;
  }

  function asContent(result) {
    return {content: [{type: 'text', text: JSON.stringify(result)}]};
  }

  const objectId = {type: 'string', description: 'An object id from qt_find (a path of object names, or #numeric).'};

  const TOOLS = [
    {
      name: 'qt_ping',
      description: 'Check that the Qt app in this page answers, with its Qt version.',
      inputSchema: {type: 'object', properties: {}},
      method: 'qt.ping',
      params: () => ({}),
    },
    {
      name: 'qt_find',
      description: "Find the app's objects by objectName and/or class name; returns their ids.",
      inputSchema: {
        type: 'object',
        properties: {
          objectName: {type: 'string'},
          className: {type: 'string'},
          limit: {type: 'integer', minimum: 1, default: 50},
        },
      },
      method: 'qt.objects.search',
      params: (input) => input,
    },
    {
      name: 'qt_get_property',
      description: "Read one Qt property of an object, such as a window's title or a label's text.",
      inputSchema: {type: 'object', properties: {objectId, name: {type: 'string'}}, required: ['objectId', 'name']},
      method: 'qt.properties.get',
      params: (input) => input,
    },
    {
      name: 'qt_click',
      description: 'Click an object, as the app receives a mouse click on its centre.',
      inputSchema: {type: 'object', properties: {objectId}, required: ['objectId']},
      method: 'qt.ui.click',
      params: (input) => input,
    },
    {
      name: 'qt_screenshot',
      description: "Capture the app's window as a base64 PNG.",
      inputSchema: {type: 'object', properties: {}},
      method: 'cu.screenshot',
      params: () => ({}),
    },
  ];

  const INVOKE = {
    name: 'qt_invoke',
    description: 'Call a slot or invokable method of an object, with positional arguments.',
    inputSchema: {
      type: 'object',
      properties: {objectId, method: {type: 'string'}, args: {type: 'array'}},
      required: ['objectId', 'method'],
    },
    method: 'qt.methods.invoke',
    params: (input) => ({objectId: input.objectId, method: input.method, args: input.args || []}),
  };

  function modelContext() {
    return (globalThis.navigator && globalThis.navigator.modelContext) ||
        (globalThis.document && globalThis.document.modelContext) || null;
  }

  function optedIn() {
    if (globalThis.qtpilotWebMcp === true) return true;
    const search = (globalThis.location && globalThis.location.search) || '';
    return /[?&]qtpilot-webmcp(=|&|$)/.test(search);
  }

  /// Registers the tools; returns their names, or [] when the page has not opted in or the
  /// browser has no modelContext.
  bridge.installWebMcp = function (options) {
    const context = modelContext();
    if (!context || !optedIn()) return [];
    bridge.uninstallWebMcp();
    registration = new AbortController();
    const tools = TOOLS.concat(options && options.allowInvoke ? [INVOKE] : []);
    for (const tool of tools) {
      context.registerTool({
        name: tool.name,
        description: tool.description,
        inputSchema: tool.inputSchema,
        execute: async (input) => asContent(call(tool.method, tool.params(input || {}))),
      }, {signal: registration.signal});
    }
    return tools.map((tool) => tool.name);
  };

  /// Unregisters every tool installWebMcp registered.
  bridge.uninstallWebMcp = function () {
    if (registration) registration.abort();
    registration = null;
  };
})();
