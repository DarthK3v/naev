/*
 * See Licensing and Copyright notice in naev.h
 */



#ifndef EQUIPMENT_H
#  define EQUIPMENT_H


#include "pilot.h"
#include "outfit.h"


/*
 * Image array names.
 */
#define  EQUIPMENT_SHIPS      "iarAvailShips"
#define  EQUIPMENT_OUTFITS    "iarAvailOutfits"


/*
 * Main.
 */
void equipment_open( unsigned int wid );
void equipment_cleanup (void);


/*
 * Misc.
 */
void equipment_addAmmo (void);
void equipment_genLists( unsigned int wid );
void equipment_updateShips( unsigned int wid, char* str );
void equipment_updateOutfits( unsigned int wid, char* str );


/*
 * Slot widget.
 */
typedef struct CstSlotWidget_ {
   Pilot *selected; /**< Selected pilot ship. */
   Outfit *outfit; /**< Selected outfit. */
   int slot; /**< Selected equipment slot. */
   int mouseover; /**< Mouse vore slot. */
   double altx; /**< Alt X text position. */
   double alty; /**< Alt Y text position. */
   int canmodify; /**< Whether or not it can modify stuff. */
} CstSlotWidget; /**< Slot widget. */
void equipment_slotWidget( unsigned int wid,
      double x, double y, double w, double h,
      CstSlotWidget *data );


#endif /* EQUIPMENT_H */
