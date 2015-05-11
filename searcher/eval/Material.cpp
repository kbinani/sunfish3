/* Material.cpp
 *
 * Kubo Ryosuke
 */

#include "Material.h"

namespace sunfish {

namespace material {

/**
 * $B6p3d$r<hF@$7$^$9!#(B
 */
Value piece(const Piece& piece) {
  switch(piece) {
    case Piece::BPawn: case Piece::WPawn:
      return material::Pawn;
    case Piece::BLance: case Piece::WLance:
      return material::Lance;
    case Piece::BKnight: case Piece::WKnight:
      return material::Knight;
    case Piece::BSilver: case Piece::WSilver:
      return material::Silver;
    case Piece::BGold: case Piece::WGold:
      return material::Gold;
    case Piece::BBishop: case Piece::WBishop:
      return material::Bishop;
    case Piece::BRook: case Piece::WRook:
      return material::Rook;
    case Piece::BTokin: case Piece::WTokin:
      return material::Tokin;
    case Piece::BProLance: case Piece::WProLance:
      return material::Pro_lance;
    case Piece::BProKnight: case Piece::WProKnight:
      return material::Pro_knight;
    case Piece::BProSilver: case Piece::WProSilver:
      return material::Pro_silver;
    case Piece::BHorse: case Piece::WHorse:
      return material::Horse;
    case Piece::BDragon: case Piece::WDragon:
      return material::Dragon;
    case Piece::BKing: case Piece::WKing:
      return Value::PieceInf;
    default:
      return 0;
  }
}

/**
 * $B6p$r<h$C$?;~$NJQ2=CM$r<hF@$7$^$9!#(B
 */
Value pieceExchange(const Piece& piece) {
  switch(piece) {
    case Piece::BPawn: case Piece::WPawn:
      return material::PawnEx;
    case Piece::BLance: case Piece::WLance:
      return material::LanceEx;
    case Piece::BKnight: case Piece::WKnight:
      return material::KnightEx;
    case Piece::BSilver: case Piece::WSilver:
      return material::SilverEx;
    case Piece::BGold: case Piece::WGold:
      return material::GoldEx;
    case Piece::BBishop: case Piece::WBishop:
      return material::BishopEx;
    case Piece::BRook: case Piece::WRook:
      return material::RookEx;
    case Piece::BTokin: case Piece::WTokin:
      return material::TokinEx;
    case Piece::BProLance: case Piece::WProLance:
      return material::Pro_lanceEx;
    case Piece::BProKnight: case Piece::WProKnight:
      return material::Pro_knightEx;
    case Piece::BProSilver: case Piece::WProSilver:
      return material::Pro_silverEx;
    case Piece::BHorse: case Piece::WHorse:
      return material::HorseEx;
    case Piece::BDragon: case Piece::WDragon:
      return material::DragonEx;
    case Piece::BKing: case Piece::WKing:
      return Value::PieceInfEx;
    default:
      return 0;
  }
}

/**
 * $B6p$,@.$C$?;~$NJQ2=CM$r<hF@$7$^$9!#(B
 */
Value piecePromote(const Piece& piece) {
  switch(piece) {
    case Piece::BPawn: case Piece::WPawn:
      return material::Tokin - material::Pawn;
    case Piece::BLance: case Piece::WLance:
      return material::Pro_lance - material::Lance;
    case Piece::BKnight: case Piece::WKnight:
      return material::Pro_knight - material::Knight;
    case Piece::BSilver: case Piece::WSilver:
      return material::Pro_silver - material::Silver;
    case Piece::BGold: case Piece::WGold:
      return 0;
    case Piece::BBishop: case Piece::WBishop:
      return material::Horse - material::Bishop;
    case Piece::BRook: case Piece::WRook:
      return material::Dragon - material::Rook;
    case Piece::BTokin: case Piece::WTokin:
    case Piece::BProLance: case Piece::WProLance:
    case Piece::BProKnight: case Piece::WProKnight:
    case Piece::BProSilver: case Piece::WProSilver:
    case Piece::BHorse: case Piece::WHorse:
    case Piece::BDragon: case Piece::WDragon:
    case Piece::BKing: case Piece::WKing:
    default:
      return 0;
  }
}

} // namespace material

} // namespace sunfish
