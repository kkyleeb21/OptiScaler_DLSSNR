"""Numerical invariants of the optional unit-luminance colour transfer."""
import numpy as np
y=np.array([.2126,.7152,.0722])
def transfer(original,proxy,model,target_luma,strength=1):
    base=original/(original@y)
    delta=(model/(model@y)-proxy/(proxy@y))*np.clip(strength,0,1)
    amount=1.
    for c in range(3):
        if delta[c]<-1e-6:amount=min(amount,base[c]/-delta[c])
    colour=base+np.clip(amount,0,1)*delta
    return colour*target_luma/max(colour@y,1e-6)
rng=np.random.default_rng(42)
for _ in range(2000):
    original,proxy,model=rng.uniform(.001,4,(3,3))
    target=float(original@y)
    assert np.allclose(transfer(original,proxy,proxy,target),original)
    assert np.allclose(transfer(original,proxy,model,target,0),original)
    result=transfer(original,proxy,model,target)
    assert result.min()>-1e-12 and np.isfinite(result).all()
    assert np.isclose(result@y,target)
print('PASS: identity model, zero strength, luminance preservation, finite nonnegative gamut')
