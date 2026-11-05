"""Run outside UE: targeted MCP graph repair; backups and per-call receipts retained."""
import sys,json,uuid,shutil
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'skills/build-ga/scripts'))
from mcp_call import MCP
c=MCP(root,'SkillIconBackground')
run=root/'Saved/GAAuthoring/SkillIconBackground'/uuid.uuid4().hex
run.mkdir()
def call(tool,**args):
 result=c.call(tool,args)
 (run/(uuid.uuid4().hex+'.json')).write_text(json.dumps(result,indent=2),encoding='utf8')
 return result
def fix(path,graph):
 data=call('blueprint_read_graph_summary',asset_path=path,graph_name=graph)
 nodes=data['graphs'][0]['nodes'];byid={n['id']:n for n in nodes}
 if any(n['title']=='SetRenderOpacity' for n in nodes):
  raise RuntimeError('Existing opacity logic: inspect before rerunning')
 shutil.copy2(root/(path.replace('/Game/','Content/')+'.uasset'),run/(path.split('/')[-1]+'.uasset'))
 def add(cls,fn,x,y):
  r=call('blueprint_add_function_call_node',asset_path=path,graph_name=graph,class_path=cls,function_name=fn,node_x=x,node_y=y)
  # Identify by difference rather than depending on undocumented mutation response keys.
  fresh=call('blueprint_read_graph_summary',asset_path=path,graph_name=graph)['graphs'][0]['nodes']
  added=[n for n in fresh if n['id'] not in byid];assert len(added)==1,added
  n=added[0];byid[n['id']]=n;return n['id']
 def link(src,pin,dst,dpin):call('blueprint_connect_pins',asset_path=path,from_node_guid=src,from_pin_name=pin,to_node_guid=dst,to_pin_name=dpin)
 def default(n,p,v):call('blueprint_set_pin_default_value',asset_path=path,node_guid=n,pin_name=p,value=v)
 targets=[n for n in nodes if (n['title']=='SetBrushFromMaterial' if graph!='SetIconAndBrush' else n['id']=='8844C0854142EF7443A5188F065FA6E0')]
 assert len(targets)==(3 if graph!='SetIconAndBrush' else 1)
 for n in targets:
  pins={p['name']:p for p in n['pins']};assert pins['then']['linked_count']==0
  x=n['x']+300;y=n['y']
  opacity=add('/Script/UMG.Widget','SetRenderOpacity',x+700,y)
  target=pins['self']['linked_pins'][0];link(target['node_id'],target['pin'],opacity,'self');link(n['id'],'then',opacity,'execute')
  if graph!='SetIconAndBrush' and not pins['Material']['linked_count']:
   default(opacity,'InOpacity','1.0' if pins['Material']['defaultObject'] else '0.0');continue
  valid=add('/Script/Engine.KismetSystemLibrary','IsValid',x,y+180)
  select=add('/Script/Engine.KismetMathLibrary','SelectFloat',x+350,y+180)
  default(select,'A','1.0');default(select,'B','0.0')
  link(valid,'ReturnValue',select,'bPickA');link(select,'ReturnValue',opacity,'InOpacity')
  if graph=='SetIconAndBrush':
   resource=add('/Script/UMG.WidgetBlueprintLibrary','GetBrushResource',x-250,y+180)
   source=pins['Brush']['linked_pins'][0];link(source['node_id'],source['pin'],resource,'Brush');link(resource,'ReturnValue',valid,'Object')
  else:
   source=pins['Material']['linked_pins'][0];link(source['node_id'],source['pin'],valid,'Object')
 call('blueprint_compile',asset_path=path)
 call('asset_save',asset_path=path,only_if_dirty=False)
fix('/Game/BluePrints/UI/SpellGlobe/WBP_SpellGlobe','SetIconAndBrush')
fix('/Game/BluePrints/UI/SpellMenu/WBP_My_SpellGLobeButton','ReceiveAbiityInfo')
print('REPAIRED',run)
