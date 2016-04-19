#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp; //*initp;
  uint nunits;
  uint best_fit = 0xffffffff;
  Header *best_p, *best_prevp;
  uint i;

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  //initp = prevp->s.ptr;
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    //printf(1, "got called 1\n");
    if(p->s.size >= nunits){
      printf(1, "got called 4\n");
      if(p->s.size == nunits) {
        printf(1, "got called 2\n");
        prevp->s.ptr = p->s.ptr;
        //best_fit = p->s.size;
        //best_p = p;
        freep = prevp;
        return (void *)(p + 1);
        //return (void*)(p + 1);
      }
      else {
        if (p->s.size < best_fit) {
          printf(1, "got called 3\n");
          i++;
          best_fit = p->s.size;
          best_p = p;
          best_prevp = prevp;
          if (i == 10)
            break;
        }
        //p->s.size -= nunits;
        //p += p->s.size;
        //p->s.size = nunits;
      }

      //freep = prevp;
      //return (void*)(p + 1);
    }
    
    if(p == prevp)
      if((p = morecore(nunits)) == 0)
        return 0;
  }

  best_prevp->s.ptr = best_p->s.ptr;
  freep = best_prevp;
  return (void *)(best_p);





  // best_p->s.size -= nunits;
  // best_p += best_p->s.size;
  // best_p->s.size = nunits;
  // best_prevp->s.ptr = best_p;
  // freep = best_prevp;
  
  // return (void *)(best_p + 1);


}