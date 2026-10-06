"""RECOVERY TRANSCRIPTION, 2026-10-05.

Compact algorithms transcribed from the completed independent P1-P5 audit code
and its visible tool results after the executor was replaced. This is NOT a
post-reset rerun and does not replace lost raw reports. It contains only read-only
measurement functions. Blender Python/mathutils and restored exact input files
are required for geometry evaluation. No Blender object, pose, mesh, or file save
is performed here.

Restore native_frame_reference.json before using the anatomy function: the
hardware and dorsal marker IDs and native dorsal offsets are external inputs.
Custom corner-normal transport was NOT verified before reset.
"""
import collections
import hashlib
import json
import math
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import intersect_ray_tri

BASELINE_N_SHA256 = 'de60108a89bfe7f2f5bb92f2b35f2d692d3a659ca7f1dbc310ca7023572a33ef'
NATIVE_STOCK_CONTACT = np.array([.0012969123, .45, .0226502232])
RECORDED_REAR_FACE_TRIANGLES = [1388,1389,1390,1391,3977,3978,4255,4256,4357,4358,4367,4368,4379,4380,4880,4881,5523,5524,5528,5529]
P = 'mixamorig:'
PARITY_DIRECTIONS = [Vector(v).normalized() for v in [(.853,.413,.318),(.31,-.91,.27),(-.41,.12,.903)]]


def evaluated_positions(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    points = [evaluated.matrix_world @ v.co for v in mesh.vertices]
    evaluated.to_mesh_clear()
    return points


def components(triangles, vertex_count):
    """Connected component ID is the minimum original vertex index."""
    parent = list(range(vertex_count))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for t in triangles:
        for i in t[1:]:
            parent[root(int(i))] = root(int(t[0]))
    groups = collections.defaultdict(list)
    for i in range(vertex_count):
        groups[root(i)].append(i)
    groups = {min(ids): ids for ids in groups.values()}
    return {v: cid for cid, ids in groups.items() for v in ids}, groups


def triangle_weight_tags(triangles, vertex_weights):
    weights = []
    for t in triangles:
        combined = collections.Counter()
        for i in t:
            for name, weight in vertex_weights[i].items():
                combined[name] += weight / 3
        weights.append(combined)
    return [w.most_common(1)[0][0] for w in weights], weights


def finite_crossings(va, ta, ia, vb, tb, ib, same=False):
    """Exact finite edge/triangle witnesses after BVH broad phase.

    This reproduces the historical helper's tolerances. Shared-vertex triangle
    pairs are excluded for same-mesh checks. Counts/chords are NOT penetration
    depths, containment tests, or complete coplanar/continuous-collision proof.
    """
    aa, bb = [ta[i] for i in ia], [tb[i] for i in ib]
    A = BVHTree.FromPolygons(va, aa, all_triangles=True)
    B = BVHTree.FromPolygons(vb, bb, all_triangles=True)
    hits = {}
    for x, y in A.overlap(B):
        at, bt = aa[x], bb[y]
        if same and set(at) & set(bt):
            continue
        key = tuple(sorted((int(ia[x]),int(ib[y])))) if same else (int(ia[x]),int(ib[y]))
        if key in hits:
            continue
        a, b = [va[k] for k in at], [vb[k] for k in bt]
        point = None
        for f, g in [(a,b),(b,a)]:
            if point is not None:
                break
            for p, q in [(0,1),(1,2),(2,0)]:
                direction = f[q] - f[p]
                length = direction.length
                if length < 1e-8:
                    continue
                h = intersect_ray_tri(*g, direction/length, f[p], True)
                if h is not None and (h-f[p]).dot(direction) > 1e-10 and (h-f[p]).length < length-1e-7:
                    point = h
                    break
        if point is not None:
            hits[key] = {'triangles':list(key),'point':list(point)}
    return hits


def crossing_chord(vertices, triangles, pair):
    """Intersection segment extent, not solid penetration depth."""
    a, b = [[vertices[i] for i in triangles[j]] for j in pair]
    found = []
    for f, g in [(a,b),(b,a)]:
        for p, q in [(0,1),(1,2),(2,0)]:
            d = f[q]-f[p]
            length = d.length
            if length < 1e-9:
                continue
            h = intersect_ray_tri(*g,d/length,f[p],True)
            if h is not None and (h-f[p]).dot(d)>1e-10 and (h-f[p]).length<length-1e-7 and not any((h-x).length<1e-7 for x in found):
                found.append(h)
    return max(((a-b).length*1000 for a in found for b in found),default=0.)


def topology_edges(triangles):
    edges = collections.Counter(tuple(sorted((int(a),int(b)))) for t in triangles for a,b in [(t[0],t[1]),(t[1],t[2]),(t[2],t[0])])
    return {'boundary':sum(n==1 for n in edges.values()),'not_exactly_two_faces':sum(n!=2 for n in edges.values())}


def parity_votes(tree, point, epsilon=.0000005, maximum_hits=200, ray_length=10.):
    votes = []
    for d in PARITY_DIRECTIONS:
        q = Vector(point)
        count = 0
        for _ in range(maximum_hits):
            h,n,t,distance = tree.ray_cast(q,d,ray_length)
            if h is None:
                break
            count += 1
            q = h+d*epsilon
        votes.append(count%2)
    return votes


def closed_component_containment(target_v, target_t, probe_v, probe_t):
    """Screen vertices and triangle centroids against a closed non-self solid.

    The caller must prove zero boundary/nonmanifold edges and screen target
    self-intersections first. Three agreeing odd-parity rays are retained.
    Empty finite sampling is not by itself universal containment proof.
    """
    v = np.asarray(target_v)
    probes = np.r_[np.asarray(probe_v),np.asarray(probe_v)[np.asarray(probe_t)].mean(1)]
    lo, hi = v.min(0)-1e-7, v.max(0)+1e-7
    ii = np.flatnonzero(np.all(probes>=lo,1)&np.all(probes<=hi,1))
    tree = BVHTree.FromPolygons([Vector(x) for x in v],target_t,all_triangles=True)
    witnesses, ambiguous = [], 0
    for i in ii:
        p = probes[i]
        h,n,t,d = tree.find_nearest(Vector(p))
        if d < .00001:
            continue
        votes = parity_votes(tree,p)
        ambiguous += 0 < sum(votes) < 3
        if sum(votes) == 3:
            witnesses.append({'probe_kind':'vertex' if i<len(probe_v) else 'triangle_centroid','probe_index':int(i if i<len(probe_v) else i-len(probe_v)),'point_m':p.tolist(),'nearest_surface_gap_mm':d*1000,'parity_votes':votes})
    return {'probe_count_in_bbox':len(ii),'all3_inside_count':len(witnesses),'ambiguous_ray_vote_count':ambiguous,'witnesses':witnesses}


def analysis_only_wrist_cap(all_vertices, hand_triangles):
    """Cap the single native16-edge wrist opening in ARRAYS only.

    Do not write these arrays into the original Blender mesh. The audit checked
    that there were16 boundary edges and excluded cap faces from anatomical
    surface-contact claims.
    """
    V, T = np.asarray(all_vertices), np.asarray(hand_triangles)
    counts = collections.Counter(tuple(sorted((int(a),int(b)))) for t in T for a,b in [(t[0],t[1]),(t[1],t[2]),(t[2],t[0])])
    boundary = [(int(a),int(b)) for t in T for a,b in [(t[0],t[1]),(t[1],t[2]),(t[2],t[0])] if counts[tuple(sorted((int(a),int(b))))]==1]
    ids = sorted({i for e in boundary for i in e})
    VV = np.r_[V,[V[ids].mean(0)]]
    TT = np.r_[T,[(b,a,len(V)) for a,b in boundary]]
    volume = np.einsum('ij,ij->i',VV[TT[:,0]],np.cross(VV[TT[:,1]],VV[TT[:,2]])).sum()/6
    if volume < 0:
        TT = TT[:,::-1]
    return VV,TT,{'boundary_edges':len(boundary),'volume_m3':abs(float(volume))}


def dense_surface_samples(vertices, triangles, bounds=None, pitch=.0015, dedup=.001):
    """Barycentric max-edge spacing <=1.5mm; 1mm rounded-coordinate dedup."""
    V = np.asarray(vertices)
    result = []
    for t in triangles:
        tv = V[t]
        if bounds is not None:
            lo, hi = bounds
            if np.any(tv.max(0)<lo) or np.any(tv.min(0)>hi):
                continue
        count = max(2,math.ceil(np.linalg.norm(tv[[1,2,0]]-tv,axis=1).max()/pitch))
        for u in range(count+1):
            for w in range(count+1-u):
                result.append(tv[0]+(tv[1]-tv[0])*u/count+(tv[2]-tv[0])*w/count)
    if not result:
        return np.empty((0,3))
    result = np.asarray(result)
    _, index = np.unique(np.round(result/dedup).astype(int),axis=0,return_index=True)
    return result[index]


def hand_inside_samples(capped_tree, weapon_samples):
    """Recorded hand method: negative nearest signed gap plus majority3 rays.

    Majority confirms sampled inside points; the observed P4/P5 deepest contacts
    had all three inside votes. Signed nearest gap is a local depth metric.
    """
    inside = []
    for p in weapon_samples:
        hit,n,j,d = capped_tree.find_nearest(Vector(p))
        signed = (Vector(p)-hit).dot(n)
        if signed >= -.0000001:
            continue
        votes = parity_votes(capped_tree,p,epsilon=.000002,maximum_hits=100,ray_length=3.)
        if sum(votes)>=2:
            inside.append({'point_m':list(p),'nearest_hand_or_cap_triangle':int(j),'depth_mm':-signed*1000,'votes':votes})
    return inside


def opposed_contact_patch(V, T, triangle_region_tags, hand_weights, hand_world_matrix, gun_tree):
    """Estimated area; palm requires Hand weight>.6 and volar normal dot>.3."""
    V, H = np.asarray(V), np.asarray(hand_world_matrix)
    patches = collections.defaultdict(lambda:{'area_within1mm_mm2':0.,'area_within2mm_mm2':0.,'min_surface_gap_mm':1e9})
    for j,t in enumerate(T):
        tv = V[t]
        n = np.cross(tv[1]-tv[0],tv[2]-tv[0])
        area = np.linalg.norm(n)/2
        n /= max(2*area,1e-20)
        region = triangle_region_tags[j]
        if region=='Palm' and (n@H[:3,2]<.3 or hand_weights[j]<.6):
            continue
        count = max(2,min(12,math.ceil(np.linalg.norm(tv[[1,2,0]]-tv,axis=1).max()/.003)))
        near1=near2=total=0
        for u in range(count+1):
            for w in range(count+1-u):
                p=tv[0]+(tv[1]-tv[0])*u/count+(tv[2]-tv[0])*w/count
                hit,normal,k,d=gun_tree.find_nearest(Vector(p))
                total+=1
                patches[region]['min_surface_gap_mm']=min(patches[region]['min_surface_gap_mm'],d*1000)
                if n@np.asarray(normal)<-.1:
                    near1+=d<.001
                    near2+=d<.002
        patches[region]['area_within1mm_mm2']+=area*1e6*near1/total
        patches[region]['area_within2mm_mm2']+=area*1e6*near2/total
    return dict(patches)


def triangle_contains_2d(q, tri):
    A,B,C=np.asarray(tri)
    u,v,w=B-A,C-A,np.asarray(q)-A
    det=u[0]*v[1]-u[1]*v[0]
    if abs(det)<1e-16:
        return False
    b=(w[0]*v[1]-w[1]*v[0])/det
    c=(u[0]*w[1]-u[1]*w[0])/det
    return min(1-b-c,b,c)>-1e-7


def uniform_rear_face(native_gun_vertices, gun_triangles, evaluated_gun_vertices, merged_body_and_pad_tree, triangle_labels):
    """Unique1mm x/z grid on actual native-y=.45 rear material.

    triangle_labels must map merged tree indices to object, triangle, and body
    component. Headgear hits must not be described as shoulder support.
    The affine fit residual must be <1micrometre before using this transform.
    """
    GL=np.asarray(native_gun_vertices)
    GT=np.asarray(gun_triangles)
    GV=np.asarray(evaluated_gun_vertices)
    fit=np.linalg.lstsq(np.c_[GL,np.ones(len(GL))],GV,rcond=None)[0]
    error=float(np.linalg.norm(np.c_[GL,np.ones(len(GL))]@fit-GV,axis=1).max())
    if error>=.000001:
        raise ValueError('Rifle affine residual exceeds recorded acceptance limit')
    pad=np.flatnonzero(np.min(GL[GT,1],axis=1)>.4499)
    projected=GL[GT[pad]][:,:,[0,2]]
    lo,hi=projected.reshape(-1,2).min(0),projected.reshape(-1,2).max(0)
    axis=Vector(fit[1]).normalized()
    rows=[]
    for ix in range(math.ceil(lo[0]*1000),math.floor(hi[0]*1000)+1):
        for iz in range(math.ceil(lo[1]*1000),math.floor(hi[1]*1000)+1):
            q=np.array([ix/1000,iz/1000])
            if not any(triangle_contains_2d(q,t) for t in projected):
                continue
            point=Vector(np.array([q[0],.45,q[1],1])@fit)
            row={'native_xz_mm':[ix,iz],'offset_from_stock_contact_mm':((q-NATIVE_STOCK_CONTACT[[0,2]])*1000).tolist(),'point_world_m':list(point)}
            h,n,k,d=merged_body_and_pad_tree.ray_cast(point-axis*.100,axis,.400)
            if h is not None:
                row.update(axial_gap_mm=(d-.100)*1000,axial_target=triangle_labels[k])
            h,n,k,d=merged_body_and_pad_tree.ray_cast(point,axis,.300)
            if h is not None:
                row.update(rearward_support_gap_mm=d*1000,rearward_support_target=triangle_labels[k])
            rows.append(row)
    return {'rifle_affine_fit_error_mm':error*1000,'rear_face_triangles':pad.tolist(),'uniform_grid_pitch_mm':1,'samples':rows}


def normalized_all_influence_lbs(rest_world_vertices, weights, posed_bone_world, rest_bone_world):
    points=np.zeros_like(np.asarray(rest_world_vertices),dtype=float)
    jacobians=np.zeros((len(points),3,3))
    for i,w in enumerate(weights):
        total=sum(w.values())
        for name,weight in w.items():
            transform=np.asarray(posed_bone_world[name])@np.linalg.inv(rest_bone_world[name])
            fraction=weight/total
            points[i]+=fraction*(transform[:3,:3]@rest_world_vertices[i]+transform[:3,3])
            jacobians[i]+=fraction*transform[:3,:3]
    return points,jacobians


def material_normal_transport(rest_vertices, evaluated_vertices, triangles, vertex_lbs_jacobians):
    """Geometric-face normal diagnostic; NOT custom corner-normal verification."""
    R=np.asarray(rest_vertices);X=np.asarray(evaluated_vertices);T=np.asarray(triangles)
    nr=np.cross(R[T[:,1]]-R[T[:,0]],R[T[:,2]]-R[T[:,0]])
    nl=np.linalg.norm(nr,axis=1);nr/=np.maximum(nl[:,None],1e-16)
    nn=np.cross(X[T[:,1]]-X[T[:,0]],X[T[:,2]]-X[T[:,0]])
    el=np.linalg.norm(nn,axis=1);nn/=np.maximum(el[:,None],1e-16)
    mean=np.asarray(vertex_lbs_jacobians)[T].mean(1)
    transported=np.einsum('nij,nj->ni',np.linalg.pinv(mean).transpose(0,2,1),nr)
    transported/=np.maximum(np.linalg.norm(transported,axis=1)[:,None],1e-16)
    return {'normal_alignment':(nn*transported).sum(1),'area_ratio':el/np.maximum(nl,1e-15)}


def radial(point,origin,axis):
    d=point-origin
    return (d-axis*d.dot(axis)).normalized()


def signed_angle(a,b,axis):
    return math.degrees(math.atan2(axis.dot(a.cross(b)),a.dot(b)))


def wrap_degrees(value):
    return (value+180)%360-180


def arm_material_frame_anatomy(rig, evaluated_body_vertices, side, references):
    """Requires restored native hardware/dorsal marker IDs and offsets.

    The original rig object matrix was identity. If restored object coordinates
    differ, put body vertices and all bone points in one common coordinate frame
    before reproducing this measurement.
    """
    a,f,h=[rig.pose.bones[P+side+k] for k in ['Arm','ForeArm','Hand']]
    V=evaluated_body_vertices
    u=(f.head-a.head).normalized();v=(h.head-f.head).normalized()
    n=u.cross(v).normalized();fy=(f.tail-f.head).normalized();hy=(h.tail-h.head).normalized()
    data=references[side]
    hardware=sum((V[i] for i in data['hardware_ids']),Vector())/len(data['hardware_ids'])
    dorsal=sum((V[i] for i in data['hand_dorsal_ids']),Vector())/len(data['hand_dorsal_ids'])
    angle=signed_angle(radial(hardware,f.head,fy),hy.rotation_difference(fy)@radial(dorsal,h.head,hy),fy)
    sign=1 if side=='Left' else -1
    hinge=signed_angle(n,radial(a.matrix.to_3x3().col[2]*sign,Vector(),u),u)
    return {'hinge_deg':hinge,'hinge_native_residual_deg':wrap_degrees(hinge-data['signed_native_hinge_offset_deg']),'dorsal_residual_deg':wrap_degrees(angle-data['native_dorsal_offset_deg']),'wrist_deg':math.degrees(v.angle(hy)),'elbow_deg':math.degrees(u.angle(v))}
