use DistributedDeque;
use DistributedBag;



// Here we test the capabilities of the utility methods, 'addBulk' and 'removeBulk'.
config param isBoundedDeque = false;
config param isDeque = false;
config param isBag = false;

config param nElems = 1000;

proc DistDequeTest() {
	var deque : DistDeque(int);
	deque.create();

	// Add Bulk Test
	var successfulInsertions = deque.addBulk(1..nElems);
	assert(successfulInsertions == nElems);
	assert(deque.getSize() == nElems);

	// Remove Bulk Test
	var iterations = 0;
	for elt in deque.removeBulk(nElems) {
		iterations += 1;
	}
	assert(iterations == nElems);

	deque.destroy();
	writeln("SUCCESS");
}

proc BoundedDistDequeTest() {
	var deque : DistDeque(int);
	deque.create(cap = nElems);

	// Add Bulk Test
	// The additional element must be rejected
	var successfulInsertions = deque.addBulk(1..nElems + 1);
	assert(successfulInsertions == nElems);
	assert(deque.getSize() == nElems);

	// Remove Bulk Test
	var iterations = 0;
	for elt in deque.removeBulk(nElems) {
		iterations += 1;
	}
	assert(iterations == nElems);

	deque.destroy();
	writeln("SUCCESS");
}

proc DistBagTest() {
	var bag : DistBag(int);
	bag.create();

	// Add Bulk Test
	// The additional element must be rejected
	var successfulInsertions = bag.addBulk(1..nElems);
	assert(successfulInsertions == nElems);
	assert(bag.getSize() == nElems);

	// Remove Bulk Test
	var iterations = 0;
	for elt in bag.removeBulk(nElems) {
		iterations += 1;
	}
	assert(iterations == nElems);

	bag.destroy();
	writeln("SUCCESS");
}


if isBoundedDeque then BoundedDistDequeTest();
else if isDeque then DistDequeTest();
else if isBag then DistBagTest();
else compilerError("Require 'isBoundedDeque', 'isDeque', or 'isBag' to be set...");
