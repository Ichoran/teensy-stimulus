package lab.kerrr.ticklish

import java.time._
import scala.util.control.NonFatal


sealed trait TicklishState { def indicator: Char }
object TicklishState {
  object Running extends TicklishState { def indicator = '*' }
  object Errored extends TicklishState { def indicator = '!' }
  object AllDone extends TicklishState { def indicator = '/' }
  object Program extends TicklishState { def indicator = '.' }
  val allStates = Vector(Running, Errored, AllDone, Program)
  val charToState = allStates.map(x => x.indicator -> x).toMap
}

object TicklishUtil {
  def encodeTime(t: Duration): String = {
    val sec = t.getSeconds
    if (sec > 99999999) "99999999"
    else if (sec == 0) f"${t.getNano.toDouble*1e-9}%.6f"
    else {
      val s = (sec*1000000000L + t.getNano).toString
      if (s.length < 16)       (s.take(s.length-9) + "." + s.drop(s.length-9)).take(8)
      else if (s.length == 16) "0" + (s take 7)
      else                     s take 8
    }
  }
  def decodeTime(s: String): Duration = {
    val dpi = s.indexOf('.')
    if (dpi < 0) Duration.ofSeconds(s.toLong)
    else {
      val sec = s.substring(1, dpi)
      val rest = s.drop(dpi+1)
      if (rest.length <= 3) Duration.ofMillis(sec.toLong*1000 + rest.padTo(3, '0').toInt)
      else Duration.ofNanos(sec.toLong*1000000000 + rest.padTo(9, '0').toInt)
    }
  }

  def decodeVoltage(s: String): Float = s.drop(1).toFloat
  def decodeBitsTH(s: String): Option[(Float, Float)] =
    if (s.length < 11) None
    else {
      import java.lang.Integer.parseInt
      val humidity = parseInt(s.substring(1, 5), 16);
      val temperature = parseInt(s.substring(5, 9), 16);
      val checksum = parseInt(s.substring(9, 11), 16)
      if ((((humidity & 0xFF) + (humidity >>> 8) + (temperature & 0xFF) | (temperature >>> 8)) & 0xFF) != checksum) None
      else Some((temperature.toFloat, humidity.toFloat))
    }

  def decodeState(s: String): TicklishState = TicklishState.charToState(s.charAt(1))

  def encodeDrift(d: Double): String = {
    val ad = math.abs(d);
    if (!(ad >= 1.00000001e-8 && ad < 1.3)) "+00000000"
    else "%c%08d".format(if (d < 0) '-' else '+', math.round(1.0/ad).toInt)
  }
  def decodeDrift(s: String): Option[Double] =
    if (s.length != 12) None
    else if (s.charAt(1) != '^') None
    else try { 
      val x = s.substring(3, 11).toDouble * (if (s.charAt(1) == '-') -1 else 1)
      Some(if (x == 0) 0 else 1.0/x)
    }
    catch { case e if NonFatal(e) => None }

  def encodeName(name: String): String = f"IDENTITY$name"
  def decodeName(s: String): (String, String) =
    if (isTicklish(s)) (s drop 12, s.substring(9,12))
    else throw new IllegalArgumentException("Unknown device type")

  def isTicklish(s: String): Boolean = s.startsWith("$Ticklish2.")

  def isTimeReport(s: String): Boolean = s.length == 16 && {
    var i = 1
    var hasDot = false
    while (i < s.length) {
      val c = s.charAt(i)
      if (c == '.') {
        if (hasDot) return false
        else hasDot = true
      }
      else if (!('0' <= c && c <= '9')) return false
      i += 1
    }
    true
  }  
}
