// Dot art for the VirtualPet cat: 24x20, one bit per pixel, row major.
// Generated from the design sketch kept with the v0.4.0 notes; a lit bit
// is a white pixel on the SSD1306, so the cat is drawn in white with the
// eyes, nose and paws left as holes.
#pragma once

#include <cstdint>

namespace fw {

struct PetSprite {
  uint8_t width;
  uint8_t height;
  uint32_t rows[24];
};

  // kCatTailUp: 24x20, bit x of a row is column x.
  //           #      #        
  //          ##      ##       
  //          ##########       
  //         ############      
  //         ############      
  //         ############      
  //         ##  ####  ##      
  //         ##  ####  ##      
  //         ############      
  //         #####  #####      
  //         #####  #####      
  //         #### ## ####      
  //         ############   #  
  //         ############   #  
  //        ##############  #  
  //        ############## #   
  //        ###############    
  //        ##############     
  //        ##############     
  //         #   ####   #      
  constexpr PetSprite kCatTailUp{
      24, 20,
      {
          0x00008100u, 0x00018180u, 0x0001FF80u,
          0x0003FFC0u, 0x0003FFC0u, 0x0003FFC0u,
          0x00033CC0u, 0x00033CC0u, 0x0003FFC0u,
          0x0003E7C0u, 0x0003E7C0u, 0x0003DBC0u,
          0x0023FFC0u, 0x0023FFC0u, 0x0027FFE0u,
          0x0017FFE0u, 0x000FFFE0u, 0x0007FFE0u,
          0x0007FFE0u, 0x00023C40u,
      },
  };

  // kCatTailMid: 24x20, bit x of a row is column x.
  //           #      #        
  //          ##      ##       
  //          ##########       
  //         ############      
  //         ############      
  //         ############      
  //         ##  ####  ##      
  //         ##  ####  ##      
  //         ############      
  //         #####  #####      
  //         #####  #####      
  //         #### ## ####      
  //         ############      
  //         ############      
  //        ##############     
  //        ##############   # 
  //        ##############  #  
  //        ################   
  //        ##############     
  //         #   ####   #      
  constexpr PetSprite kCatTailMid{
      24, 20,
      {
          0x00008100u, 0x00018180u, 0x0001FF80u,
          0x0003FFC0u, 0x0003FFC0u, 0x0003FFC0u,
          0x00033CC0u, 0x00033CC0u, 0x0003FFC0u,
          0x0003E7C0u, 0x0003E7C0u, 0x0003DBC0u,
          0x0003FFC0u, 0x0003FFC0u, 0x0007FFE0u,
          0x0047FFE0u, 0x0027FFE0u, 0x001FFFE0u,
          0x0007FFE0u, 0x00023C40u,
      },
  };

  // kCatTailDown: 24x20, bit x of a row is column x.
  //           #      #        
  //          ##      ##       
  //          ##########       
  //         ############      
  //         ############      
  //         ############      
  //         ##  ####  ##      
  //         ##  ####  ##      
  //         ############      
  //         #####  #####      
  //         #####  #####      
  //         #### ## ####      
  //         ############      
  //         ############      
  //        ##############     
  //        ##############     
  //        ##############   # 
  //        ##############  #  
  //        ################   
  //         #   ####   #      
  constexpr PetSprite kCatTailDown{
      24, 20,
      {
          0x00008100u, 0x00018180u, 0x0001FF80u,
          0x0003FFC0u, 0x0003FFC0u, 0x0003FFC0u,
          0x00033CC0u, 0x00033CC0u, 0x0003FFC0u,
          0x0003E7C0u, 0x0003E7C0u, 0x0003DBC0u,
          0x0003FFC0u, 0x0003FFC0u, 0x0007FFE0u,
          0x0007FFE0u, 0x0047FFE0u, 0x0027FFE0u,
          0x001FFFE0u, 0x00023C40u,
      },
  };

  // kIconHeart: 5x5, floating above a happy cat.
  //           # #  
  //          ##### 
  //          ##### 
  //           ###  
  //            #   
  constexpr PetSprite kIconHeart{
      5, 5,
      {
          0x0Au, 0x1Fu, 0x1Fu, 0x0Eu, 0x04u,
      },
  };

  // kIconNote: 5x5, a music note for the reaction to a pat.
  //            ###
  //            #  
  //            #  
  //          # #  
  //          ###  
  constexpr PetSprite kIconNote{
      5, 5,
      {
          0x18u, 0x08u, 0x08u, 0x0Au, 0x07u,
      },
  };

  // kIconSparkle: 5x5, a four point sparkle.
  //            #   
  //            #   
  //          ##### 
  //            #   
  //            #   
  constexpr PetSprite kIconSparkle{
      5, 5,
      {
          0x04u, 0x04u, 0x1Fu, 0x04u, 0x04u,
      },
  };

}  // namespace fw
