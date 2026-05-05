/**
 * Shared JavaScript for Solar App
 */

function toggleTheme() {
    const body = document.body;
    const btn = document.getElementById('themeToggle');
    const isLight = body.classList.toggle('light-mode');
    if (btn) btn.textContent = isLight ? '🌙' : '☀️';
    localStorage.setItem('theme', isLight ? 'light' : 'dark');
    
    if (typeof updateChart === 'function') {
        setTimeout(() => { if(typeof lastStatus !== 'undefined') updateChart(lastStatus); }, 100);
    }
    if (typeof renderCharts === 'function') {
        renderCharts();
    }
}

function initTheme() {
    if (localStorage.getItem('theme') === 'light') {
        document.body.classList.add('light-mode');
        const btn = document.getElementById('themeToggle');
        if (btn) btn.textContent = '🌙';
    }
}

function updateDateTime() {
    const now = new Date();
    const dateStr = now.toLocaleDateString('fr-FR');
    const timeStr = now.toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
    const el = document.getElementById('navDateTime');
    if (el) el.textContent = dateStr + ' ' + timeStr;
}

function injectNavbar() {
    const path = window.location.pathname;
    const nav = document.createElement('nav');
    nav.className = 'navbar';
    
    // Group Left: Brand + Time + Theme
    const leftGroup = document.createElement('div');
    leftGroup.style.display = 'flex';
    leftGroup.style.alignItems = 'center';
    leftGroup.style.gap = '15px';
    
    const brand = document.createElement('a');
    brand.href = '/';
    brand.className = 'navbar-brand';
    brand.innerHTML = '☀️ Solaire';
    leftGroup.appendChild(brand);

    const timeSpan = document.createElement('span');
    timeSpan.id = 'navDateTime';
    timeSpan.style.fontSize = '0.8em';
    timeSpan.style.color = 'var(--muted-text)';
    timeSpan.textContent = '--/--/---- --:--';
    leftGroup.appendChild(timeSpan);

    const themeBtn = document.createElement('button');
    themeBtn.id = 'themeToggle';
    themeBtn.textContent = localStorage.getItem('theme') === 'light' ? '🌙' : '☀️';
    themeBtn.style.background = 'none';
    themeBtn.style.border = '1px solid var(--border-color)';
    themeBtn.style.color = 'var(--accent-color)';
    themeBtn.style.padding = '4px 8px';
    themeBtn.style.borderRadius = '20px';
    themeBtn.style.cursor = 'pointer';
    themeBtn.style.fontSize = '1.1em';
    themeBtn.onclick = toggleTheme;
    leftGroup.appendChild(themeBtn);

    nav.appendChild(leftGroup);
    
    // Group Right: Links
    const links = document.createElement('div');
    links.className = 'navbar-links';
    links.style.display = 'flex';
    links.style.alignItems = 'center';
    links.style.gap = '10px';

    const createLink = (href, text, id = '') => {
        const a = document.createElement('a');
        a.href = href;
        a.textContent = text;
        if (id) a.id = id;
        if (path === href || (path === '/' && href === '/')) a.className = 'active';
        return a;
    };
    
    links.appendChild(createLink('/', 'Dashboard'));
    links.appendChild(createLink('/web_config', 'Config'));

    const devLink = createLink('/web_dev', 'Dev');
    devLink.style.display = (path === '/web_config') ? 'inline-block' : 'none';
    links.appendChild(devLink);
    
    const statsLink = createLink('/stats', 'Stats', 'navStatsLink');
    statsLink.style.display = (path === '/stats') ? 'none' : 'none';
    links.appendChild(statsLink);
    
    const resetLink = document.createElement('a');
    resetLink.href = '#';
    resetLink.textContent = 'Reset';
    resetLink.style.color = '#e74c3c';
    resetLink.style.display = (path === '/web_config') ? 'inline-block' : 'none';
    resetLink.onclick = (e) => {
        e.preventDefault();
        if (confirm('Redémarrer le device ?')) window.location.href = '/RESET_device';
    };
    links.appendChild(resetLink);
    
    nav.appendChild(links);
    document.body.insertBefore(nav, document.body.firstChild);
    
    // Start time update
    updateDateTime();
    setInterval(updateDateTime, 10000);
}

// Auto-init on load
document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    injectNavbar();
});
