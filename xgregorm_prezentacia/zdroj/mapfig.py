import re, xml.etree.ElementTree as ET, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import Circle
B='/home/marti/VaN3Twin/ns-3-dev/src/automotive/examples/xgregorm_city_center/'
OUT='/tmp/claude-1000/-home-marti-VaN3Twin-ns-3-dev/ec75cf74-daad-4aea-ada7-981509d7544d/scratchpad/pres/img/'
net=ET.parse(B+'xgregorm_map.net.xml').getroot()
rsu={}
for l in open(B+'stations.xml'):
    m=re.search(r'id="poi_(\d+)".*x="([-\d.]+)" y="([-\d.]+)"',l)
    if m: rsu[int(m[1])]=(float(m[2]),float(m[3]))
W='#2A9D8F'; N='#E76F51'
def draw(zones,fn,title):
    fig,ax=plt.subplots(figsize=(6,6),dpi=200)
    for e in net.iter('edge'):
        if e.get('function')=='internal': continue
        for ln in e.iter('lane'):
            pts=[tuple(map(float,p.split(','))) for p in ln.get('shape').split()]
            ax.plot(*zip(*pts),color='#9AA5AB',lw=0.6,zorder=1)
    for r,t in zones:
        x,y=rsu[r]; c=W if t=='W' else N
        ax.add_patch(Circle((x,y),150,color=c,alpha=0.22,zorder=0,lw=0))
        ax.add_patch(Circle((x,y),150,fill=False,ec=c,lw=1.5,zorder=2))
    for i,(x,y) in rsu.items():
        ax.plot(x,y,'o',ms=9,color='#0B3C49',zorder=3)
        ax.text(x+18,y+18,f'RSU {i}',fontsize=9,color='#0B3C49',zorder=4,fontweight='bold')
    ax.set_aspect('equal'); ax.axis('off')
    fig.savefig(OUT+fn,bbox_inches='tight',transparent=False,facecolor='white'); plt.close(fig)
draw([(0,'W'),(2,'N'),(5,'W'),(8,'N')],'mapA.png','A')
draw([],'map.png','')
print(rsu)
