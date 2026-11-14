try{const saved=localStorage.getItem('pbc-theme');if(saved)document.body.dataset.theme=saved}catch(e){}const theme=document.getElementById('theme');theme.value=document.body.dataset.theme||'night';theme.addEventListener('change',()=>{document.body.dataset.theme=theme.value;try{localStorage.setItem('pbc-theme',theme.value)}catch(e){}});const guideArticles=[...document.querySelectorAll('article.guide')];
function showGuide(){
 if(!guideArticles.length)return;
 const requested=decodeURIComponent(location.hash.slice(1));
 const selected=guideArticles.find(a=>a.id===requested||requested.startsWith(a.id+'--'));
 const library=document.getElementById('guide-library'),reader=document.getElementById('guide-reader');
 if(!library||!reader)return;
 library.hidden=!!selected;reader.hidden=!selected;
 guideArticles.forEach(a=>a.hidden=a!==selected);
 if(!selected)return;
 const illustration=document.getElementById('reader-illustration');illustration.src=selected.dataset.art;
 const outline=JSON.parse(selected.dataset.outline||'[]');
 document.getElementById('guide-outline-links').replaceChildren(...outline.map(([id,title])=>{const a=document.createElement('a');a.href='#'+id;a.textContent=title;return a}));
 reader.querySelectorAll('.guide-menu a,.guide-mobile-nav a').forEach(a=>{if(a.getAttribute('href')==='#'+selected.id)a.setAttribute('aria-current','true');else a.removeAttribute('aria-current')});
 const pager=document.getElementById('guide-pager'),index=guideArticles.indexOf(selected);pager.replaceChildren();
 for(const [offset,label] of [[-1,pager.dataset.previous],[1,pager.dataset.next]]){
  const item=guideArticles[index+offset];if(!item)continue;
  const a=document.createElement('a'),small=document.createElement('small');a.href='#'+item.id;small.textContent=label;a.append(small,document.createTextNode(item.dataset.title));pager.append(a);
 }
 reader.querySelectorAll('details').forEach(d=>d.open=false);
 if(requested===selected.id)window.scrollTo(0,0);
}
window.addEventListener('hashchange',showGuide);showGuide();
const guideSearch=document.getElementById('guide-search');
if(guideSearch){const normalize=s=>s.normalize('NFD').replace(/[\u0300-\u036f]/g,'').toLocaleLowerCase();guideSearch.addEventListener('input',()=>{const query=normalize(guideSearch.value.trim());let visible=0;document.querySelectorAll('[data-guide-card]').forEach(card=>{card.hidden=!normalize(card.dataset.search).includes(query);if(!card.hidden)visible++});document.querySelectorAll('.guide-group').forEach(group=>group.hidden=![...group.querySelectorAll('[data-guide-card]')].some(card=>!card.hidden));document.getElementById('guide-empty').hidden=visible>0})}
document.getElementById('language').addEventListener('change',e=>{const c=e.target.value;let fragment='';if(document.body.dataset.page==='guides'&&guideArticles.length&&document.getElementById('guide-reader')&&!document.getElementById('guide-reader').hidden){const shown=guideArticles.find(a=>!a.hidden)||guideArticles[0];const base=shown.id.split('.')[0];const suffix=c==='en'?'':(c==='fr'||c==='fr-CA')?'.fr':'.'+c;fragment='#'+base+suffix}location.href=document.body.dataset.page+'.'+c+'.html'+fragment});
document.querySelectorAll("[data-theme-choice]").forEach(b=>b.addEventListener("click",()=>{const picker=document.getElementById("theme");picker.value=b.dataset.themeChoice;picker.dispatchEvent(new Event("change"))}));


const mediaLanguage=document.getElementById('media-video-language');
if(mediaLanguage){const video=document.getElementById('media-video-player'),audio=document.getElementById('media-audio-player');mediaLanguage.addEventListener('change',()=>{const code=mediaLanguage.value;video.pause();video.src='media/teaser-preview.'+code+'.mp4';video.poster=code==='fr'||code==='fr-CA'?'assets/tv-home-current.png':'assets/en-oled-home.png';video.load();document.getElementById('media-video-download').href='media/teaser-preview.'+code+'.mp4';document.getElementById('media-video-label').textContent=mediaLanguage.options[mediaLanguage.selectedIndex].text});video.addEventListener('play',()=>audio.pause());audio.addEventListener('play',()=>video.pause())}

function syncPaletteButtons(){const theme=document.body.dataset.theme||"night";document.querySelectorAll("[data-theme-choice]").forEach(b=>b.setAttribute("aria-pressed",String(b.dataset.themeChoice===theme)))}document.getElementById("theme").addEventListener("change",syncPaletteButtons);syncPaletteButtons();

const themeCapture = document.querySelector('[data-theme-capture]');
if (themeCapture) {
  const captures = {
    night: 'theme-night.png',
    oled: 'theme-oled.png',
    playstation: 'theme-playstation.png',
    sakura: 'theme-colour.png',
    light: 'theme-light.png'
  };
  const image = themeCapture.querySelector('img');
  const link = themeCapture.querySelector('a');
  const caption = themeCapture.querySelector('figcaption');
  const originalImage = image.getAttribute('src');
  const originalCaption = caption.textContent;
  const originalAlt = image.alt;

  function showThemeCapture() {
    const selection = document.body.dataset.theme || 'night';
    const selectedCapture = captures[selection];
    const source = selectedCapture && selection !== 'night'
      ? 'assets/' + selectedCapture : originalImage;
    const name = (theme.selectedOptions[0] || theme.options[0]).text;
    image.src = source;
    link.href = source;
    image.alt = originalAlt + ' — ' + (selectedCapture ? name : theme.options[0].text);
    caption.textContent = selectedCapture && selection !== 'night' ? name : originalCaption;
  }

  theme.addEventListener('change', showThemeCapture);
  showThemeCapture();
}
