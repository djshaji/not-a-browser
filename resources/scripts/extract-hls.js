(() => {
    const found = new Set();
    const limit = 64;
    const add = raw => {
        if (typeof raw !== 'string' || found.size >= limit || raw.length > 8192) return;
        raw = raw.trim();
        if (/[\s"'<>\\]/.test(raw)) return;
        try {
            const url = new URL(raw.trim(), document.baseURI);
            if (['http:', 'https:'].includes(url.protocol) && !url.username && !url.password &&
                /\.m3u8$/i.test(url.pathname)) {
                url.hash = '';
                found.add(url.href);
            }
        } catch (_) {}
    };
    const scan = text => {
        if (!text || found.size >= limit || /^(?:javascript|data|blob):/i.test(text.trim())) return;
        text = text.replace(/\\\//g, '/').replace(/\\u002f/gi, '/')
            .replace(/\\u0026/gi, '&').replace(/&amp;/gi, '&');
        add(text);
        // Quoted strings cover relative URLs and JSON player configuration.
        for (const match of text.matchAll(/["']([^"'\s<>]*\.m3u8(?:\?[^"'\s<>]*)?)["']/gi)) add(match[1]);
        for (const match of text.matchAll(/(?:https?:)?\/\/[^\s"'<>\\]+\.m3u8(?:\?[^\s"'<>\\]*)?/gi)) add(match[0]);
    };
    for (const video of document.querySelectorAll('video,audio')) {
        add(video.currentSrc);
        add(video.src);
    }
    let inspected = 0;
    let budget = 2 * 1024 * 1024;
    for (const element of document.querySelectorAll('*')) {
        if (++inspected > 20000 || budget <= 0 || found.size >= limit) break;
        for (const attribute of element.attributes) {
            if (budget <= 0 || found.size >= limit) break;
            if (attribute.value.length > 65536) continue;
            budget -= attribute.value.length;
            scan(attribute.value);
        }
        if (element.tagName === 'SCRIPT') {
            const text = element.textContent.slice(0, Math.max(0, budget));
            budget -= text.length;
            scan(text);
        }
    }
    return [...found].join('\n');
})();
