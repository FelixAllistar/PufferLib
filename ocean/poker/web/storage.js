export const SESSION_KEY='puffer-poker.cash.v1';
const integer=(n,max=1e9)=>Number.isInteger(n)&&n>=0&&n<=max;
export function loadSession(){
  try{
    const raw=sessionStorage.getItem(SESSION_KEY);if(!raw)return null;
    if(raw.length>1000000)throw Error('Oversized saved session');
    const s=JSON.parse(raw),n=s.settings?.players,o=s.settings?.opponent;
    if(s.version!==1||![2,6,9].includes(n)||!['mixed','rules','range','fullhouse','river'].includes(o)||n===9&&o==='fullhouse'||n!==2&&o==='river'||![80,600,900].includes(s.settings.pace)||!integer(s.buyins)||s.buyins<20000||!integer(s.recordedHand,1e7)||!s.session||!['hands','wins','won','lost'].every(k=>integer(s.session[k]))||!Array.isArray(s.tableBuyins)||s.tableBuyins.length!==n||!s.tableBuyins.every(v=>integer(v))||s.checkpoint?.players!==n||!Array.isArray(s.history)||s.history.length>120||!Array.isArray(s.lastActions)||s.lastActions.length>n)throw Error('Invalid saved session');
    s.history=s.history.filter(h=>['actor','text','amount','kind'].every(k=>typeof h[k]==='string'&&h[k].length<250));
    s.lastActions=s.lastActions.map(v=>typeof v==='string'?v.slice(0,150):'');
    return s;
  }catch{return {invalid:true};}
}
export function saveSession(value){try{sessionStorage.setItem(SESSION_KEY,JSON.stringify(value));return true;}catch{return false;}}
