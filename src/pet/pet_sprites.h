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

}  // namespace fw
