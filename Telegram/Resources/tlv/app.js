/*
 * Local TL JSON viewer for AywGram Desktop.
 * The runtime schema parser is derived from the TL serialization rules used by
 * Telegram Desktop. The viewer never evaluates schema text or message data.
 */
(() => {
  'use strict';

  const MAX_BYTES = 16 * 1024 * 1024;
  const MAX_VECTOR = 10000;
  const MAX_NODES = 100000;
  const MAX_DEPTH = 64;
  const VECTOR_ID = 0x1cb5c415;
  const TRUE_ID = 0x997275b5;
  const FALSE_ID = 0xbc799737;
  const status = document.getElementById('status');
  const output = document.getElementById('output');
  const copy = document.getElementById('copy');
  let json = '';

  const fail = message => {
    json = '';
    output.textContent = '';
    status.textContent = String(message || 'Unable to decode this object.');
    copy.disabled = true;
  };

  const splitFields = source => {
    const result = [];
    let token = '';
    let angle = 0;
    let brace = 0;
    for (const ch of source.trim()) {
      if (/\s/.test(ch) && angle === 0 && brace === 0) {
        if (token) result.push(token), token = '';
      } else {
        token += ch;
        if (ch === '<') ++angle;
        if (ch === '>') --angle;
        if (ch === '{') ++brace;
        if (ch === '}') --brace;
      }
    }
    if (token) result.push(token);
    return result;
  };

  const parseSchema = source => {
    const constructors = new Map();
    for (const original of source.split(/\r?\n/)) {
      const line = original.replace(/\/\/.*$/, '').trim();
      if (!line || line.startsWith('---')) continue;
      const match = /^([\w.]+)#([0-9a-fA-F]+)\s*(.*?)\s*=\s*([^;]+);$/.exec(line);
      if (!match) continue;
      const fields = [];
      for (const token of splitFields(match[3])) {
        if (token.startsWith('{') || token === '#' || token === '[' || token === ']') continue;
        const colon = token.indexOf(':');
        if (colon <= 0) continue;
        fields.push({ name: token.slice(0, colon), type: token.slice(colon + 1) });
      }
      constructors.set(parseInt(match[2], 16) >>> 0, { name: match[1], fields });
    }
    return constructors;
  };

  const decodeBase64Url = value => {
    if (typeof value !== 'string' || value.length > MAX_BYTES * 2) throw new Error('Invalid payload.');
    const normalized = value.replace(/-/g, '+').replace(/_/g, '/');
    const binary = atob(normalized + '='.repeat((4 - normalized.length % 4) % 4));
    if (binary.length > MAX_BYTES) throw new Error('Message is too large to display.');
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i !== binary.length; ++i) bytes[i] = binary.charCodeAt(i);
    return bytes;
  };

  const encodeBase64 = bytes => {
    let binary = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
    }
    return btoa(binary);
  };

  class Reader {
    constructor(bytes, constructors) {
      this.bytes = bytes;
      this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      this.schema = constructors;
      this.offset = 0;
      this.nodes = 0;
    }
    require(size) {
      if (size < 0 || this.offset + size > this.bytes.length) throw new Error('Unexpected end of TL data.');
    }
    uint() { this.require(4); const v = this.view.getUint32(this.offset, true); this.offset += 4; return v; }
    int() { this.require(4); const v = this.view.getInt32(this.offset, true); this.offset += 4; return v; }
    long() {
      this.require(8);
      const low = BigInt(this.view.getUint32(this.offset, true));
      const high = BigInt(this.view.getInt32(this.offset + 4, true));
      this.offset += 8;
      return ((high << 32n) | low).toString();
    }
    double() { this.require(8); const v = this.view.getFloat64(this.offset, true); this.offset += 8; return v; }
    raw(size) { this.require(size); const v = this.bytes.slice(this.offset, this.offset + size); this.offset += size; return v; }
    bytesValue() {
      this.require(1);
      let size = this.bytes[this.offset++];
      let header = 1;
      if (size === 254) {
        this.require(3);
        size = this.bytes[this.offset] | (this.bytes[this.offset + 1] << 8) | (this.bytes[this.offset + 2] << 16);
        this.offset += 3;
        header = 4;
      }
      const value = this.raw(size);
      const padding = (4 - ((header + size) % 4)) % 4;
      this.require(padding);
      this.offset += padding;
      return value;
    }
    string() { return new TextDecoder('utf-8', { fatal: false }).decode(this.bytesValue()); }
    hex(size) { return Array.from(this.raw(size), x => x.toString(16).padStart(2, '0')).join(''); }
    vector(type, depth) {
      if (this.uint() !== VECTOR_ID) throw new Error('Invalid vector constructor.');
      const count = this.int();
      if (count < 0 || count > MAX_VECTOR) throw new Error('Vector is too large to display.');
      const result = [];
      for (let i = 0; i !== count; ++i) result.push(this.value(type, depth + 1));
      return result;
    }
    value(type, depth) {
      type = type.replace(/^!/, '').trim();
      if (type === 'int' || type === '#') return this.int();
      if (type === 'long') return this.long();
      if (type === 'double') return this.double();
      if (type === 'string') return this.string();
      if (type === 'bytes') return encodeBase64(this.bytesValue());
      if (type === 'int128') return this.hex(16);
      if (type === 'int256') return this.hex(32);
      if (type === 'true') return true;
      const vector = /^(?:Vector|vector)<(.+)>$/.exec(type);
      if (vector) return this.vector(vector[1], depth);
      return this.object(depth + 1);
    }
    object(depth = 0) {
      if (depth > MAX_DEPTH || ++this.nodes > MAX_NODES) throw new Error('TL object is too complex to display.');
      const id = this.uint();
      if (id === TRUE_ID) return true;
      if (id === FALSE_ID) return false;
      const constructor = this.schema.get(id);
      if (!constructor) throw new Error(`Unknown constructor 0x${id.toString(16).padStart(8, '0')}.`);
      const result = { _: constructor.name };
      const flags = Object.create(null);
      for (const field of constructor.fields) {
        const conditional = /^([\w]+)\.(\d+)\?(.+)$/.exec(field.type);
        if (conditional) {
          const bits = flags[conditional[1]];
          if (bits === undefined) throw new Error(`Missing flags field ${conditional[1]}.`);
          if (!((bits >>> Number(conditional[2])) & 1)) continue;
          if (conditional[3] === 'true') { result[field.name] = true; continue; }
          result[field.name] = this.value(conditional[3], depth);
        } else {
          const value = this.value(field.type, depth);
          result[field.name] = value;
          if (field.type === '#') flags[field.name] = value >>> 0;
        }
      }
      return result;
    }
  }

  const sendClipboard = text => {
    const message = JSON.stringify({ type: 'clipboard_write', text });
    if (window.external && typeof window.external.invoke === 'function') window.external.invoke(message);
  };

  copy.addEventListener('click', () => {
    const selection = String(window.getSelection() || '');
    sendClipboard(selection || json);
  });

  window.TelegramDesktopTLV = Object.freeze({
    load(params) {
      try {
        if (!params || params.layer !== params.schemaLayer) throw new Error('The message layer does not match the bundled TL schema.');
        document.documentElement.dataset.dark = params.dark ? '1' : '0';
        const reader = new Reader(decodeBase64Url(params.payload), parseSchema(params.schema));
        const object = reader.object();
        if (reader.offset !== reader.bytes.length) throw new Error('Trailing bytes after the TL object.');
        json = JSON.stringify(object, null, 2);
        output.textContent = json;
        status.textContent = '';
        copy.disabled = false;
      } catch (error) {
        fail(error instanceof Error ? error.message : error);
      }
    },
    setTheme(dark) { document.documentElement.dataset.dark = dark ? '1' : '0'; },
  });
})();
