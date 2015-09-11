#!/usr/bin/env runhaskell

import Data.List
import Data.Maybe
import System.IO
import System.Process

settings_file_name = "settings.h"
peaq_cmd = "./peaq --gst-plugin-load=.libs/libgstpeaq.so"
conf_data_path = "../BS.1387-ConformanceDatabase"

data Item = Item { refname :: String, codname :: String,
                   di_basic :: Double, odg_basic :: Double,
                   di_advanced :: Double, odg_advanced::Double } deriving Show

conformance_items :: [Item]
conformance_items = [
  --      refname       codname    di_basic odg_Basic di_advanced odg_advanced
  Item "arefsna.wav" "acodsna.wav" ( 1.304)  (-0.676)    ( 1.632)     (-0.467),
  Item "breftri.wav" "bcodtri.wav" ( 1.949)  (-0.304)    ( 2.000)     (-0.281),
  Item "crefsax.wav" "ccodsax.wav" ( 0.048)  (-1.829)    ( 0.567)     (-1.300),
  Item "erefsmg.wav" "ecodsmg.wav" ( 1.731)  (-0.412)    ( 1.594)     (-0.489),
  Item "frefsb1.wav" "fcodsb1.wav" ( 0.677)  (-1.195)    ( 1.039)     (-0.877),
  Item "freftr1.wav" "fcodtr1.wav" ( 1.419)  (-0.598)    ( 1.555)     (-0.512),
  Item "freftr2.wav" "fcodtr2.wav" (-0.045)  (-1.927)    ( 0.162)     (-1.711),
  Item "freftr3.wav" "fcodtr3.wav" (-0.715)  (-2.601)    (-0.78 )     (-2.662),
  Item "grefcla.wav" "gcodcla.wav" ( 1.781)  (-0.386)    ( 1.457)     (-0.573),
  Item "irefsna.wav" "icodsna.wav" (-3.029)  (-3.786)    (-2.51 )     (-3.664),
  Item "krefsme.wav" "kcodsme.wav" ( 3.093)  ( 0.038)    ( 2.765)     (-0.029),
  Item "lrefhrp.wav" "lcodhrp.wav" ( 1.041)  (-0.876)    ( 1.538)     (-0.523),
  Item "lrefpip.wav" "lcodpip.wav" ( 1.973)  (-0.293)    ( 2.149)     (-0.219),
  Item "mrefcla.wav" "mcodcla.wav" (-0.436)  (-2.331)    ( 0.430)     (-1.435),
  Item "nrefsfe.wav" "ncodsfe.wav" ( 3.135)  ( 0.045)    ( 3.163)     ( 0.050),
  Item "srefclv.wav" "scodclv.wav" ( 1.689)  (-0.435)    ( 1.972)     (-0.293)]

itemFileNames :: Item -> [String]
itemFileNames i = map ((conf_data_path ++ "/") ++) [refname i, codname i]

getDefines :: String -> [String]
getDefines contents
  = mapMaybe getDefine $ lines contents
    where getDefine line | ("#define":n:_) <- words line = Just n
                         | otherwise = Nothing

substituteDefines :: String -> [(String, Int)] -> String
substituteDefines contents subs
  = unlines $ map substLine $ lines contents
    where substLine line | ("#define":n:_) <- words line,
                           Just v <- lookup n subs
                           = unwords ["#define", n, show v]
                         | otherwise = line

flagCombinations :: Int -> [[Int]]
flagCombinations 0 = [[]]
flagCombinations n = [f:fs | fs <- flagCombinations (n-1), f <- [0, 1]]

newDefines :: [String] -> [[(String, Int)]]
newDefines defines = [zip defines fc | fc <- fcs]
                     where fcs = flagCombinations $ length defines

parsePeaqOutput :: String -> (Double, Double)
parsePeaqOutput output = (di, odg)
  where extract s = read $ head $ mapMaybe (stripPrefix s) $ lines output
        di = extract "Distortion Index:"
        odg = extract "Objective Difference Grade:"

invokePeaq :: [String] -> IO (Double, Double)
invokePeaq args
    = let cmd = intercalate " " (peaq_cmd:args)
      in do (_, Just hout, _, _) <- createProcess (shell cmd){ std_out = CreatePipe }
            output <- hGetContents hout
            return $ parsePeaqOutput output

rmse :: [Double] -> [Double] -> Double
rmse as bs = let se_sum = sum $ map (**2) $ zipWith (-) as bs
                 len = fromIntegral $ length as
             in sqrt (se_sum / len)

applySettings :: String -> [(String, Int)] -> IO ()
applySettings orig_settings new_defines
  = let new_settings = substituteDefines orig_settings new_defines
    in do writeFile settings_file_name new_settings
          (_, _, _, ph) <- createProcess (shell "make"){ std_out = CreatePipe }
          waitForProcess ph
          return ()

evaluateSettings :: String -> [(String, Int)] -> IO (Double, Double)
evaluateSettings orig_settings new_defines
  = do applySettings orig_settings new_defines
       putStr $ intercalate " " $ map (show . snd) new_defines
       putStr " "
       resultsBasic <- sequence [invokePeaq $ itemFileNames i
                                 | i <- conformance_items]
       let basic_di_rmse = rmse (map fst resultsBasic) (map di_basic conformance_items)
       putStr $ shows basic_di_rmse " "
       resultsAdvanced <- sequence [invokePeaq ("--advanced":itemFileNames i)
                                    | i <- conformance_items]
       let advanced_di_rmse = rmse (map fst resultsAdvanced) (map di_advanced conformance_items)
       putStrLn $ show advanced_di_rmse
       return (basic_di_rmse, advanced_di_rmse)

main = do contents <- readFile settings_file_name
          let new_defs = newDefines $ getDefines contents
          di_rmses <- sequence $ map (evaluateSettings contents) new_defs
          let best_di_basic = minimum $ map fst di_rmses
              best_di_advanced = minimum $ map snd di_rmses
              best_defs = map fst $
                          filter ((==best_di_advanced) . snd . snd) $
                          filter ((==best_di_basic) . fst . snd) $
                          zip new_defs di_rmses
              best_def = if null best_defs
                         then error "No settings optimal for both basic and advanced version!"
                         else head best_defs
          applySettings contents best_def
