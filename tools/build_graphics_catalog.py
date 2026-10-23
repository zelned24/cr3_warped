"""Build a local, searchable QA catalog from the original graphic inventories."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    data=json.loads((ROOT/'game_data/objects/manifest.json').read_text())
    models=[]
    sources={}
    for level in data['levels']:
        for pair in level['pairs']:
            sources.setdefault(pair['asset'],[]).append(level['id'])
    for identity,record in data['models'].items():
        models.append(dict(record,asset=identity,levels=sorted(set(sources.get(identity,[])))))
    payload=json.dumps({'models':models,'levels':data['levels'],'totals':data.get('totals',{})},ensure_ascii=True).replace('<','\\u003c')
    template='''<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Crash 3 · Catálogo de gráficos originales</title>
<style>body{margin:0;background:#0b1420;color:#e9f4ff;font:15px system-ui}header{padding:24px;background:#14273a;border-bottom:3px solid #e39832}h1{font-size:24px;margin:0 0 8px}p{color:#afc8db;max-width:1000px;line-height:1.5}input,select,button{background:#102235;color:#e9f4ff;border:1px solid #50778c;border-radius:6px;padding:10px;margin:4px}main{padding:20px}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px}.card{background:#152334;border:1px solid #29485d;border-radius:8px;padding:12px;overflow:hidden}.card img{width:112px;height:112px;image-rendering:pixelated;display:block;margin:auto}.card a{color:#7be7ef;text-decoration:none}.small{font-size:12px;color:#9fbacb;overflow-wrap:anywhere}.warn{color:#ffbd6f}nav a{color:#7be7ef;margin-right:20px}#count{padding:12px 0}details{margin-top:6px}</style>
<header><h1>Crash 3 · Gráficos del disco</h1><p>Catálogo de revisión de modelos, texturas y fotogramas recuperados. Una vista previa exportada no acredita que el personaje, enemigo o vehículo esté integrado en la lógica del juego. Las referencias no resueltas permanecen en los manifiestos.</p><nav><a href="bottom_ui.png">Panel inferior</a><a href="toad_textured_software.png">Entrada texturada (software)</a><a href="../../README.md">Estado de integración</a></nav></header>
<main><input id="search" placeholder="Buscar TGEO, SVTX o identificador" aria-label="Buscar modelo"><select id="level"><option value="">Todos los archivos</option></select><label><input type="checkbox" id="warnings">Solo con advertencias</label><button id="more">Mostrar 100 más</button><div id="count"></div><div id="cards" class="grid"></div></main>
<script>const DATA=__DATA__;let limit=100;const $=id=>document.getElementById(id);for(const entry of DATA.levels){const o=document.createElement('option');o.value=entry.id;o.textContent='Archivo 0x'+entry.id.toString(16).toUpperCase().padStart(2,'0');$('level').append(o)}function render(){const q=$('search').value.toLowerCase(),level=$('level').value;const found=DATA.models.filter(m=>(!level||m.levels.includes(Number(level)))&&JSON.stringify([m.geo,m.svtx,m.asset]).toLowerCase().includes(q)&&(!$('warnings').checked||(m.warnings&&m.warnings.length)));$('count').textContent=found.length+' modelos únicos coincidentes · '+DATA.levels.length+' archivos revisados · '+Math.min(limit,found.length)+' mostrados';$('cards').replaceChildren();for(const m of found.slice(0,limit)){const card=document.createElement('article');card.className='card';const img=document.createElement('img');img.loading='lazy';img.src='../game_data/objects/'+m.path+'/preview.png';img.alt=m.geo+' / '+m.svtx;const a=document.createElement('a');a.href='../game_data/objects/'+m.path+'/mesh.json';a.textContent=m.geo+' / '+m.svtx;const info=document.createElement('p');info.className='small';info.textContent='Archivos: '+m.levels.map(n=>'0x'+n.toString(16).toUpperCase()).join(', ');card.append(img,a,info);if(m.warnings&&m.warnings.length){const d=document.createElement('details'),t=document.createElement('summary');t.textContent=m.warnings.length+' advertencias';d.className='small warn';d.append(t,document.createTextNode(JSON.stringify(m.warnings)));card.append(d)}$('cards').append(card)}}for(const id of ['search','level','warnings'])$(id).addEventListener('input',()=>{limit=100;render()});$('more').onclick=()=>{limit+=100;render()};render();</script></html>'''
    output=ROOT/'capturas/graphics_catalog.html'
    output.write_text(template.replace('__DATA__',payload),encoding='utf-8')
    print(output,len(models),'unique models')

if __name__=='__main__':main()
