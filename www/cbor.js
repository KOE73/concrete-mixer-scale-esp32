(function () {
  function decode(payload) {
    if (!window.CBOR || typeof window.CBOR.decode !== 'function') {
      throw new Error('cbor-x is not loaded');
    }
    const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
    return window.CBOR.decode(bytes);
  }

  async function fetchState() {
    const response = await fetch('/api/state.cbor', {
      headers: { Accept: 'application/cbor' }
    });
    if (!response.ok) {
      throw new Error(await response.text());
    }
    return decode(await response.arrayBuffer());
  }

  window.MixerScaleCbor = { decode, fetchState };
}());
