class MyDist : BaseDist {
  def newArithmeticDom(param rank: int, type idxType, param stridable: bool) {
    return new MyDom(rank=rank, idxType=idxType);
  }
}

class MyDom : BaseArithmeticDom {
  param rank: int;
  type idxType = int(32);

  def getIndices() {
    var r: range;
    return r;
  }

  def setIndices(ind) {
  }
}
