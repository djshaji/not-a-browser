(adapter => {
    const found = new Set();
    if (adapter.interfaceVersion !== 1 || adapter.id !== 'generic-html-hls')
        throw new Error('Unsupported discovery adapter');
    const settings = adapter.discovery.playlistCandidates;
    const limit = Math.min(64, settings.maxCandidates);
    const resolve = raw => {
        if (typeof raw !== 'string' || raw.length > 8192) return null;
        raw = raw.trim();
        if (!raw || /[\s"'<>\\]/.test(raw)) return null;
        try {
            const url = new URL(raw, document.baseURI);
            if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password) return null;
            url.hash = '';
            return url.href;
        } catch (_) { return null; }
    };
    const add = raw => {
        if (found.size >= limit) return;
        const url = resolve(raw);
        if (url && new URL(url).pathname.toLowerCase().endsWith(settings.urlPathExtension.toLowerCase())) found.add(url);
    };
    const scan = text => {
        if (!text || found.size >= limit || /^(?:javascript|data|blob):/i.test(text.trim())) return;
        for (const match of text.matchAll(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g)) {
            try {
                const literal = match[0];
                const value = literal[0] === '"' ? JSON.parse(literal) : literal.slice(1, -1)
                    .replace(/\\\//g, '/').replace(/\\u([0-9a-f]{4})/gi, (_, hex) => String.fromCharCode(parseInt(hex, 16)));
                add(value);
            } catch (_) {}
        }
    };
    let inspected = 0;
    let budget = 2 * 1024 * 1024;
    for (const source of settings.sources) {
        for (const element of document.querySelectorAll(source.selector)) {
            if (++inspected > 20000 || budget <= 0 || found.size >= limit) break;
            const value = source.text ? element.textContent : element.getAttribute(source.attribute);
            if (!value || value.length > budget) continue;
            budget -= value.length;
            if (source.extract === 'quoted-m3u8-urls') scan(value);
            else add(value);
        }
    }
    let poster = null;
    for (const source of adapter.discovery.poster.sources) {
        for (const element of document.querySelectorAll(source.selector)) {
            const url = resolve(element.getAttribute(source.attribute));
            if (url) { poster = {url, source: source.selector}; break; }
        }
        if (poster) break;
    }
    return {pageUrl: location.href, playlistCandidates: [...found], poster};
})
