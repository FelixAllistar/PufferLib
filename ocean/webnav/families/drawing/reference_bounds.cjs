// Independent fixture derivation from the pinned D3 curve implementation.
// Run from repository root. Samples cubic segments densely; runtime uses
// analytic extrema, so this is a separate numeric oracle (0.003 px tolerance).
const d3=require('../../../../build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/core/d3.v4.min.js');
function bounds(points){
 let controls=[points[0],points[0]],anchor=points[0];
 for(const p of points.slice(1)){
  if((p[0]-anchor[0])**2+(p[1]-anchor[1])**2>100){controls.push(p);anchor=p;}
  else controls[controls.length-1]=p;
 }
 let last=null,min=[Infinity,Infinity],max=[-Infinity,-Infinity];
 function add(x,y){min[0]=Math.min(min[0],x);min[1]=Math.min(min[1],y);max[0]=Math.max(max[0],x);max[1]=Math.max(max[1],y);}
 const context={moveTo(x,y){last=[x,y];add(x,y)},lineTo(x,y){last=[x,y];add(x,y)},closePath(){},
  bezierCurveTo(x1,y1,x2,y2,x3,y3){
   const [x0,y0]=last;
   for(let i=1;i<=4096;i++){const t=i/4096,u=1-t;add(u**3*x0+3*u*u*t*x1+3*u*t*t*x2+t**3*x3,u**3*y0+3*u*u*t*y1+3*u*t*t*y2+t**3*y3);}
   last=[x3,y3];
  }};
 d3.line().curve(d3.curveBasis).context(context)(controls);
 return [max[0]-min[0],max[1]-min[1]];
}
const mouse=x=>Math.floor(Math.round(x*256)/256);
function quality(sd){for(const [limit,reward]of [[2,1],[4,.9],[7,.7],[10,.5],[13,.3],[16,.1],[19,-.1],[22,-.3],[25,-.5],[28,-.7],[31,-.9]])if(sd<limit)return reward;return -1;}
const result=[];
for(const radius of [2,6,10,16,22,28,34,40,46,52,58,64]){
 const d=radius/Math.sqrt(2),points=Array.from({length:32},(_,i)=>{
  if(!(i%2))return [75,55];
  const k=i%8;return [mouse(75+(k===1||k===7?d:-d)),mouse(55+(k<5?d:-d))];
 });
 const distances=points.map(([x,y])=>Math.hypot(x-75,y-55)),mean=distances.reduce((a,b)=>a+b,0)/distances.length;
 const sd=Math.sqrt(distances.reduce((a,b)=>a+(b-mean)**2,0)/distances.length);
 const [width,height]=bounds(points),small=Math.min(width,height),size=small<5?-.25:small<10?.25:small<20?.5:1;
 result.push({radius,width,height,sd,reward:size<0?size:size*quality(sd)});
}
console.log(JSON.stringify(result,null,2));
