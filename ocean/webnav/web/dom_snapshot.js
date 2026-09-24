/* A bounded public DOM projection, not the complete browser AX algorithm.
 * name_source and omitted/truncated let downstream code detect information loss. */
(() => {
  const refs = new WeakMap(); let nextRef = 1;
  const ref = el => {if(!refs.has(el)) refs.set(el,nextRef++);return refs.get(el)};
  const norm = text => (text || '').replace(/\s+/g,' ').trim();
  const role = (el, degraded=false) => {
    const r=degraded?null:el.getAttribute('role');
    if(r) return ({button:1,checkbox:2,textbox:3,link:4,combobox:5,radio:6,option:7})[r]||0;
    if(el.tagName==='BUTTON')return 1;
    if(el.tagName==='A'&&el.hasAttribute('href'))return 4;
    if(el.tagName==='SELECT')return 5;
    if(el.tagName==='TEXTAREA')return 3;
    if(el.tagName==='INPUT')return ({checkbox:2,radio:6,button:1,submit:1,reset:1,hidden:0})[el.type]??3;
    return 0;
  };
  window.webnavDOM = (rootSelector='#area', instructionSelector='#query', quality=0, popupSelectors=[]) => {
    if(!Number.isInteger(quality)||quality<0||quality>3)throw Error('Unknown observation quality');
    if(!Array.isArray(popupSelectors)||popupSelectors.some(s=>typeof s!=='string'||!s.trim()))throw Error('Invalid popup scope');
    const root=document.querySelector(rootSelector);if(!root)throw Error('Missing observation root');
    // Portals are opt-in public widget roots, never an implicit whole-page scan.
    // The caller must exclude benchmark HUD/reward/private-state containers.
    const candidates=new Set(root.querySelectorAll('*'));
    for(const selector of popupSelectors)for(const popup of document.querySelectorAll(selector)){
      candidates.add(popup);for(const el of popup.querySelectorAll('*'))candidates.add(el);
    }
    const all=[...candidates].sort((a,b)=>a===b?0:a.compareDocumentPosition(b)&Node.DOCUMENT_POSITION_FOLLOWING?-1:1).filter(el=>{const b=el.getBoundingClientRect(),s=getComputedStyle(el);return b.width>0&&b.height>0&&s.display!=='none'&&s.visibility!=='hidden'&&(role(el)||el.hasAttribute('role')||el.hasAttribute('tabindex')||el.hasAttribute('onclick')||/^(LABEL|H1|H2|H3|P|LI|TABLE|TR|TD|TH)$/.test(el.tagName)||(el.childElementCount===0&&s.cursor==='pointer'))});
    const actionable=el=>!!role(el)||el.hasAttribute('onclick')||el.tabIndex>=0||getComputedStyle(el).cursor==='pointer';
    const visible=el=>{const b=el.getBoundingClientRect(),s=getComputedStyle(el);return b.width>0&&b.height>0&&s.display!=='none'&&s.visibility!=='hidden'&&b.bottom>0&&b.right>0&&b.top<innerHeight&&b.left<innerWidth};
    // Reserve context space; retain DOM order after prioritizing visible controls.
    const selected=new Set(all.filter(el=>actionable(el)&&visible(el)).slice(0,96));
    for(const el of all)if(selected.size<128)selected.add(el);
    const kept=all.filter(el=>selected.has(el)), keep=new Set(kept);let truncated=0,budget=24000;
    const utf8=new TextEncoder();
    const clip=(s,collapse=true)=>{s=collapse?norm(s):String(s||'');if(s.includes('\0'))throw Error('NUL is outside DOM-v2 text');let out='',used=0;for(const c of s){const bytes=utf8.encode(c).length;if(used+bytes>512||bytes>budget){truncated++;break}out+=c;used+=bytes;budget-=bytes}return out};
    const instruction=clip(document.querySelector(instructionSelector)?.textContent);
    const nodes=kept.map(el=>{
      const r=role(el,quality>0),b=el.getBoundingClientRect(),style=getComputedStyle(el);
      let name='',source=0;
      const ids=el.getAttribute('aria-labelledby');
      if(quality===0&&ids){name=norm(ids.split(/\s+/).map(id=>document.getElementById(id)?.textContent||'').join(' '));if(name)source=1}
      if(quality===0&&!name&&el.hasAttribute('aria-label')){name=norm(el.getAttribute('aria-label'));if(name)source=1}
      if(quality<2&&!name&&el.labels?.length){name=norm([...el.labels].map(x=>x.textContent).join(' '));if(name)source=2}
      if(!name&&(r===1||r===4||r===7||getComputedStyle(el).cursor==='pointer')){name=norm(el.innerText||el.value);if(name)source=3}
      if(!name){name=norm(el.getAttribute('placeholder')||el.getAttribute('title'));if(name)source=4}
      let p=el.parentElement;while(p&&!keep.has(p))p=p.parentElement;
      const visible=b.width>0&&b.height>0&&style.visibility!=='hidden'&&style.display!=='none'&&b.bottom>0&&b.right>0&&b.top<innerHeight&&b.left<innerWidth;
      const enabled=!el.matches(':disabled')&&el.getAttribute('aria-disabled')!=='true';
      const checked=!!el.checked||el.getAttribute('aria-checked')==='true';
      const actionable=!!r||el.hasAttribute('onclick')||el.tabIndex>=0||style.cursor==='pointer';
      if(quality===3&&ref(el)%3===0){name='';source=0;}
      return {ref:ref(el),parent:p?ref(p):0,role:r,flags:(visible?1:0)|(enabled?2:0)|(checked?4:0)|(document.activeElement===el?8:0)|(actionable?16:0),name_source:source,bounds:[b.x,b.y,b.width,b.height],name:clip(name),value:clip(el.value||'',false),text:clip(el.innerText||el.textContent||'')};
    });
    return {version:2,quality,instruction,nodes,omitted:all.length-kept.length,truncated};
  };
  return true;
})()
