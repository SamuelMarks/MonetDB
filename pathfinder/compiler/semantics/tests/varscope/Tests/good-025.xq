typeswitch ("foo")
  case $a as xs:integer return $a + 1
  default $b return 42 + $b
