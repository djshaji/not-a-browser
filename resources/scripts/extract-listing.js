// Async function body. `adapter` is embedded JSON; operation, knownUrls, and
// requestToken are WebKit function arguments. Observers clean up on cancellation.
document.dispatchEvent(new Event('stream-gallery-listing-cancel-all'));
if (adapter.interfaceVersion !== 1 || !['link', 'button'].includes(adapter.pagination.type))
    throw new Error('Unsupported listing profile');
const resolveUrl = raw => {
    if (typeof raw !== 'string' || !raw.trim() || raw.length > 8192 || /[\s<>"'\\]/.test(raw.trim())) return '';
    try {
        const url = new URL(raw.trim(), document.baseURI);
        if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password) return '';
        url.hash = '';
        return url.href;
    } catch (_) { return ''; }
};
const collect = () => {
    const items = [], seen = new Set();
    let inspected = 0;
    for (const card of document.querySelectorAll(adapter.selectors.card)) {
        if (++inspected > 20000 || items.length >= 120) break;
        const link = card.matches(adapter.selectors.link) ? card : card.querySelector(adapter.selectors.link);
        const url = resolveUrl(link && link.getAttribute('href'));
        if (!url || /\.m3u8$/i.test(new URL(url).pathname) || seen.has(url)) continue;
        seen.add(url);
        const titleElement = card.querySelector(adapter.selectors.title);
        const text = titleElement ? titleElement.textContent : (link.getAttribute('title') || link.textContent);
        const title = [...(text || '').replace(/\s+/g, ' ').trim()].slice(0, 1024).join('') || url;
        const image = card.querySelector(adapter.selectors.poster);
        const posterUrl = image ? resolveUrl(image.getAttribute('src')) || resolveUrl(image.getAttribute('data-src')) : '';
        items.push({url, title, posterUrl});
    }
    const control = document.querySelector(adapter.pagination.selector);
    const nextUrl = adapter.pagination.type === 'link' && control ? resolveUrl(control.getAttribute('href')) : '';
    const more = adapter.pagination.type === 'button' && control && !control.disabled && control.getAttribute('aria-disabled') !== 'true';
    return {pageUrl: location.href, items, nextUrl, more: Boolean(more), controlPresent: Boolean(control)};
};
// Native session state is authoritative across calls and document navigations.
// Supplying it explicitly also avoids depending on isolated-world globals.
const delivered = new Set(Object.keys(knownUrls));
const snapshot = () => {
    const result = collect();
    const unseen = result.items.filter(item => !delivered.has(item.url));
    result.items = unseen.slice(0, Math.min(24, adapter.limits.maxItemsPerPage));
    result.buffered = unseen.length > result.items.length;
    if (adapter.pagination.type === 'button' && result.buffered) result.more = true;
    return result;
};
return new Promise((resolve, reject) => {
    let observer, timer, finished = false;
    const initial = snapshot();
    const finish = (result, error) => {
        if (finished) return;
        finished = true;
        if (observer) observer.disconnect();
        clearTimeout(timer);
        document.removeEventListener('stream-gallery-listing-cancel', onCancel);
        document.removeEventListener('stream-gallery-listing-cancel-all', cancel);
        if (error) reject(new Error(error));
        else resolve(result);
    };
    const cancel = () => finish(null, 'Listing extraction cancelled');
    const onCancel = event => { if (event.detail === requestToken) cancel(); };
    document.addEventListener('stream-gallery-listing-cancel', onCancel);
    document.addEventListener('stream-gallery-listing-cancel-all', cancel, {once: true});
    observer = new MutationObserver(() => {
        const result = snapshot();
        if (operation === 'more' ? result.items.length > 0 || !result.controlPresent : result.items.length > 0)
            finish(result);
    });
    observer.observe(document.documentElement, {childList: true, subtree: true, attributes: true, characterData: true});
    timer = setTimeout(() => finish(snapshot()), Math.min(8000, adapter.limits.waitMilliseconds));
    if (operation === 'more') {
        const button = document.querySelector(adapter.pagination.selector);
        if (initial.items.length || !button || !initial.more) finish(initial);
        else button.click();
    } else if (initial.items.length || initial.nextUrl) finish(initial);
});
