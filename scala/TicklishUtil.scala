package lab.kerrr.ticklish

import java.time._

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
    else if (sec == 0) f"${t.getNano.toDouble}%8.6f"
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
      val sec = s.take(dpi)
      val rest = s.drop(dpi+1)
      if (rest <= 3) Duration.ofMillis(sec.toLong*1000 + rest.padTo(3, '0').toString)
      else Duration.ofNanos(sec.toLong*1000000000 + rest.padTo(9, '0').toString)
    }
  }

  def decodeVoltage(s: String): Float = s.toFloat
  def decodeState(s: String): TicklishState = TicklishState.charToState(s.head)

  def encodeName(name: String): String = f"IDENTITY$name"
  def decodeName(s: String): String =
    if (isTicklish(name)) name drop 8
    else throw new IllegalArgumentException("Unknown device type")

  def isTicklish(s: String): Boolean = s.startsWith("Ticklish1.0 ")

  def isTimeReport(s: String): Boolean = s.length == 15 && {
    var i = 0
    var hasDot = false
    while (i < s.length) {
      val c = s.charAt(i)
      if (c == '.') {
        if (hasDot) return false
        else hasDot = true
      }
      else if (!('0' <= c && c <= '9')) return false
    }
    true
  }  
}
