struct Node {
Request req;
RetVal ret;
boolean wait;
boolean completed;
Node *next;
};
Node *Tail;
// Tail initially points to a dummy node
// with value ⟨⊥, ⊥,FALSE, FALSE, null⟩

// The following variable is private to each thread pi; it is a pointer to a
// struct of type Node with initial value ⟨⊥, ⊥,FALSE, FALSE, null⟩
Node *nodei;
RetVal CC-Synch(Request req) { // Pseudocode for thread pi
Node *nextNode, *curNode, *tmpNode, *tmpNodeNext;
int counter = 0;
 nextNode = nodei; // pi uses a (possibly recycled) node
 nextNode→next = null;
 nextNode→wait = TRUE;
 nextNode→completed = FALSE;
 curNode = Swap(Tail, nextNode); // curNode is assigned to pi
 curNode→req = req; // pi announces its request
 curNode→next = nextNode;
 nodei = curNode; // reuse this node next time
 while (curNode→wait == TRUE) // pi spins until it is unlocked
nop;
 if (curNode→completed==TRUE)// if pi’s req is already applied
 return curNode → ret; // pi returns its return value
 tmpNode = curNode; // pi is the combiner
 while (tmpNode → next ̸=null AND counter < h) {
 counter = counter + 
 tmpNodeNext=tmpNode→next;
 apply tmpNode→req to object’s state
and store the return value to tmpNode→ret;
 tmpNode→completed = TRUE;// tmpNode’s req is applied
 tmpNode→wait = FALSE; // unlock the spinning thread
 tmpNode = tmpNodeNext; // and proceed to next node
}
 tmpNode→wait = FALSE; // unlock next node’s owner
 return curNode→ret;
}